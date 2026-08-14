#include "prefixsetuprunner.h"

#include "fluorinepaths.h"

#include "gamedetection.h"
#include "slrmanager.h"
#include "steamdetection.h"
#include "prefixsymlinks.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <log.h>

#include <algorithm>
#include <csignal>
#include <sys/types.h>

namespace
{
QString linuxPathToWineZPath(const QString& path)
{
  QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  clean.replace(QLatin1Char('/'), QLatin1Char('\\'));
  return QStringLiteral("Z:") + clean;
}
}

// ============================================================================
// Constants
// ============================================================================

static const char* WINETRICKS_URL =
    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks";

static const char* CABEXTRACT_URL =
    "https://github.com/SulfurNitride/NaK/releases/download/Cabextract/"
    "cabextract-linux-x86_64.zip";

static const char* SEVENZIP_URL =
    "https://github.com/ip7z/7zip/releases/download/26.00/7z2600-linux-x86.tar.xz";

static const char* DOTNET9_X86_URL =
    "https://builds.dotnet.microsoft.com/dotnet/Runtime/9.0.14/"
    "dotnet-runtime-9.0.14-win-x86.exe";
static const char* DOTNET9_X64_URL =
    "https://builds.dotnet.microsoft.com/dotnet/Runtime/9.0.14/"
    "dotnet-runtime-9.0.14-win-x64.exe";

static const char* DOTNET10_SDK_URL =
    "https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.201/"
    "dotnet-sdk-10.0.201-win-x64.exe";

static const char* NUGET_CONFIG_TEMPLATE = R"(<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <config>
    <add key="signatureValidationMode" value="accept" />
  </config>
  <packageSources>
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" protocolVersion="3" />
  </packageSources>
  <trustedSigners>
    <repository name="nuget.org" serviceIndex="https://api.nuget.org/v3/index.json">
      <certificate fingerprint="0E5F38F57DC1BCC806D8494F4F90FBCEDD988B46760709CBEEC6F4219AA6157D" hashAlgorithm="SHA256" allowUntrustedRoot="true" />
      <certificate fingerprint="5A2901D6ADA3D18260B9C6DFE2133C95D74B9EEF6AE0E5DC334C8454D1477DF4" hashAlgorithm="SHA256" allowUntrustedRoot="true" />
      <certificate fingerprint="1F4B311D9ACC115C8DC8018B5A49E00FCE6DA8E2855F9F014CA6F34570BC482D" hashAlgorithm="SHA256" allowUntrustedRoot="true" />
    </repository>
  </trustedSigners>
</configuration>
)";

static const char* CERT_IMPORTER_CSPROJ = R"(<?xml version="1.0" encoding="utf-8"?>
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
  </PropertyGroup>
</Project>
)";

static const char* CERT_IMPORTER_PROGRAM = R"(using System.Security.Cryptography.X509Certificates;

if (args.Length % 2 != 0)
{
    Console.Error.WriteLine("Expected pairs: <store> <pem-bundle>");
    return 2;
}

for (var i = 0; i < args.Length; i += 2)
{
    ImportBundle(args[i], args[i + 1]);
}

return 0;

static void ImportBundle(string storeName, string bundlePath)
{
    if (!File.Exists(bundlePath))
    {
        Console.WriteLine($"Missing bundle: {bundlePath}");
        return;
    }

    using var store = new X509Store(storeName, StoreLocation.CurrentUser);
    store.Open(OpenFlags.ReadWrite);

    var imported = 0;
    foreach (var cert in ReadPemBundle(bundlePath))
    {
        var existing = store.Certificates.Find(
            X509FindType.FindByThumbprint, cert.Thumbprint, validOnly: false);
        if (existing.Count > 0)
        {
            cert.Dispose();
            continue;
        }

        store.Add(cert);
        cert.Dispose();
        ++imported;
    }

    Console.WriteLine($"Imported {imported} certificate(s) into {storeName}");
}

static IEnumerable<X509Certificate2> ReadPemBundle(string bundlePath)
{
    var text = File.ReadAllText(bundlePath);
    const string begin = "-----BEGIN CERTIFICATE-----";
    const string end = "-----END CERTIFICATE-----";
    var offset = 0;

    while (true)
    {
        var beginIndex = text.IndexOf(begin, offset, StringComparison.Ordinal);
        if (beginIndex < 0)
        {
            yield break;
        }

        beginIndex += begin.Length;
        var endIndex = text.IndexOf(end, beginIndex, StringComparison.Ordinal);
        if (endIndex < 0)
        {
            yield break;
        }

        var base64 = text.Substring(beginIndex, endIndex - beginIndex)
            .Replace("\r", "")
            .Replace("\n", "")
            .Trim();

        yield return X509CertificateLoader.LoadCertificate(Convert.FromBase64String(base64));
        offset = endIndex + end.Length;
    }
}
)";

// d3dcompiler_47: prebuilt DLLs from Mozilla's fxc2 repo (Windows 8.1 SDK redist).
static const char* D3DCOMPILER_47_32_URL =
    "https://github.com/mozilla/fxc2/raw/master/dll/d3dcompiler_47_32.dll";
static const char* D3DCOMPILER_47_32_SHA256 =
    "2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851";
static const char* D3DCOMPILER_47_64_URL =
    "https://github.com/mozilla/fxc2/raw/master/dll/d3dcompiler_47.dll";
static const char* D3DCOMPILER_47_64_SHA256 =
    "4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3";

// DirectX End-User Runtimes (June 2010) — shared by d3dcompiler_43, d3dx9,
// d3dx11_43, xact, xact_x64.
static const QStringList DIRECTX_JUN2010_URLS = {
    // Current link exposed by Microsoft's official Download Center (id=8109).
    QStringLiteral(
        "https://download.microsoft.com/download/8/4/a/"
        "84a35bf1-dafe-4ae8-82af-ad2ae20b6b14/"
        "directx_Jun2010_redist.exe"),
    // Independent mirror and its archived copy.  The mirror has returned 403
    // intermittently, so keep the immutable archive as the final fallback.
    QStringLiteral(
        "https://files.holarse-linuxgaming.de/mirrors/microsoft/"
        "directx_Jun2010_redist.exe"),
    QStringLiteral(
        "https://web.archive.org/web/20260218142109id_/"
        "https://files.holarse-linuxgaming.de/mirrors/microsoft/"
        "directx_Jun2010_redist.exe"),
};
static const QStringList DIRECTX_JUN2010_SHA256 = {
    // Microsoft republished the signed package in 2024.
    QStringLiteral("053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b"),
    // Original package still served by the fallback archive.
    QStringLiteral("8746ee1a84a083a90e37899d71d50d5c7c015e69688a466aa80447f011780c0d"),
};

// Visual C++ 2015-2022 redistributable.
static const char* VCRUN2022_X86_URL = "https://aka.ms/vs/17/release/vc_redist.x86.exe";
static const char* VCRUN2022_X64_URL = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
static const QStringList VCRUN2022_X86_SHA256 = {
    QStringLiteral("0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64"),
};
static const QStringList VCRUN2022_X64_SHA256 = {
    QStringLiteral("cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b"),
};

// .NET runtimes (x86 + x64 pairs for Wine prefix).
static const char* DOTNET6_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/727d79cb-6a4c-4a6b-bd9e-af99ad62de0b/"
    "5cd3550f1589a2f1b3a240c745dd1023/dotnet-runtime-6.0.36-win-x86.exe";
static const char* DOTNET6_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/1a5fc50a-9222-4f33-8f73-3c78485a55c7/"
    "1cb55899b68fcb9d98d206ba56f28b66/dotnet-runtime-6.0.36-win-x64.exe";

static const char* DOTNET7_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/b2e820bd-b591-43df-ab10-1eeb7998cc18/"
    "661ca79db4934c6247f5c7a809a62238/dotnet-runtime-7.0.20-win-x86.exe";
static const char* DOTNET7_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/be7eaed0-4e32-472b-b53e-b08ac3433a22/"
    "fc99a5977c57cbfb93b4afb401953818/dotnet-runtime-7.0.20-win-x64.exe";

static const char* DOTNET8_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/3210417e-ab32-4d14-a152-1ad9a2fcfdd2/"
    "da097cee5aa85bd79b6d593e3866fb7f/dotnet-runtime-8.0.12-win-x86.exe";
static const char* DOTNET8_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/136f4593-e3cd-4d52-bc25-579cdf46e80c/"
    "8b98c1347293b48c56c3a68d72f586a1/dotnet-runtime-8.0.12-win-x64.exe";

static const QStringList DOTNET6_X86_SHA256 = {
    QStringLiteral("3b3cb4636251a582158f4b6b340f20b3861e6793eb9a3e64bda29cbf32da3604"),
};
static const QStringList DOTNET6_X64_SHA256 = {
    QStringLiteral("6bdad7bc4c41fe93d4ae7b0312b1d017cfe369d28e7e2e421f5b675f9feefe84"),
};
static const QStringList DOTNET7_X86_SHA256 = {
    QStringLiteral("9bf79c94ab014b555167e61f3ce653fdf54c70bda6d6c74ab9f6f44652947a89"),
};
static const QStringList DOTNET7_X64_SHA256 = {
    QStringLiteral("10f48feee0f7fb4c2ed61ecef5e58699743afc9531f8a293680a99fc2d0a78a5"),
};
static const QStringList DOTNET8_X86_SHA256 = {
    QStringLiteral("eb0d8f39fa2dbb4ff3ff72ad325b6030773df875ab509824ea18c87a368985fa"),
};
static const QStringList DOTNET8_X64_SHA256 = {
    QStringLiteral("a7c394e6ee4e8104d7a01f78103700052cc504370941b7f620e3aa5afbbc61df"),
};
static const QStringList DOTNET9_X86_SHA256 = {
    QStringLiteral("a99eb555e90eaa6703efa20f3b8fe676a9ba24bd24d439f016c20815d4ff815c"),
};
static const QStringList DOTNET9_X64_SHA256 = {
    QStringLiteral("690f85d3592edb3e5810f8cd8b4a630bbaa2a8abc354061d44d7d6c0f0d8a0dd"),
};

static const QStringList VCRUN_DLLS = {
    QStringLiteral("concrt140"),
    QStringLiteral("msvcp140"),
    QStringLiteral("msvcp140_1"),
    QStringLiteral("msvcp140_2"),
    QStringLiteral("msvcp140_atomic_wait"),
    QStringLiteral("msvcp140_codecvt_ids"),
    QStringLiteral("vcamp140"),
    QStringLiteral("vccorlib140"),
    QStringLiteral("vcomp140"),
    QStringLiteral("vcruntime140"),
    QStringLiteral("vcruntime140_1"),
};

static const QStringList DIRECTX_NATIVE_DLLS = {
    QStringLiteral("d3dcompiler_42"),
    QStringLiteral("d3dcompiler_43"),
    QStringLiteral("d3dcompiler_47"),
    QStringLiteral("d3dx9_24"),
    QStringLiteral("d3dx9_25"),
    QStringLiteral("d3dx9_26"),
    QStringLiteral("d3dx9_27"),
    QStringLiteral("d3dx9_28"),
    QStringLiteral("d3dx9_29"),
    QStringLiteral("d3dx9_30"),
    QStringLiteral("d3dx9_31"),
    QStringLiteral("d3dx9_32"),
    QStringLiteral("d3dx9_33"),
    QStringLiteral("d3dx9_34"),
    QStringLiteral("d3dx9_35"),
    QStringLiteral("d3dx9_36"),
    QStringLiteral("d3dx9_37"),
    QStringLiteral("d3dx9_38"),
    QStringLiteral("d3dx9_39"),
    QStringLiteral("d3dx9_40"),
    QStringLiteral("d3dx9_41"),
    QStringLiteral("d3dx9_42"),
    QStringLiteral("d3dx9_43"),
    QStringLiteral("d3dx10_33"),
    QStringLiteral("d3dx10_34"),
    QStringLiteral("d3dx10_35"),
    QStringLiteral("d3dx10_36"),
    QStringLiteral("d3dx10_37"),
    QStringLiteral("d3dx10_38"),
    QStringLiteral("d3dx10_39"),
    QStringLiteral("d3dx10_40"),
    QStringLiteral("d3dx10_41"),
    QStringLiteral("d3dx10_42"),
    QStringLiteral("d3dx10_43"),
    QStringLiteral("d3dx11_42"),
    QStringLiteral("d3dx11_43"),
};

static const QStringList DIRECTX_NATIVE_BUILTIN_DLLS = {
    QStringLiteral("xaudio2_0"),
    QStringLiteral("xaudio2_1"),
    QStringLiteral("xaudio2_2"),
    QStringLiteral("xaudio2_3"),
    QStringLiteral("xaudio2_4"),
    QStringLiteral("xaudio2_5"),
    QStringLiteral("xaudio2_6"),
    QStringLiteral("xaudio2_7"),
    QStringLiteral("x3daudio1_0"),
    QStringLiteral("x3daudio1_1"),
    QStringLiteral("x3daudio1_2"),
    QStringLiteral("x3daudio1_3"),
    QStringLiteral("x3daudio1_4"),
    QStringLiteral("x3daudio1_5"),
    QStringLiteral("x3daudio1_6"),
    QStringLiteral("x3daudio1_7"),
    QStringLiteral("xapofx1_1"),
    QStringLiteral("xapofx1_2"),
    QStringLiteral("xapofx1_3"),
    QStringLiteral("xapofx1_4"),
    QStringLiteral("xapofx1_5"),
    QStringLiteral("xactengine2_0"),
    QStringLiteral("xactengine2_1"),
    QStringLiteral("xactengine2_2"),
    QStringLiteral("xactengine2_3"),
    QStringLiteral("xactengine2_4"),
    QStringLiteral("xactengine2_5"),
    QStringLiteral("xactengine2_6"),
    QStringLiteral("xactengine2_7"),
    QStringLiteral("xactengine2_8"),
    QStringLiteral("xactengine2_9"),
    QStringLiteral("xactengine2_10"),
    QStringLiteral("xactengine3_0"),
    QStringLiteral("xactengine3_1"),
    QStringLiteral("xactengine3_2"),
    QStringLiteral("xactengine3_3"),
    QStringLiteral("xactengine3_4"),
    QStringLiteral("xactengine3_5"),
    QStringLiteral("xactengine3_6"),
    QStringLiteral("xactengine3_7"),
};

// Allowed drive letters to keep in the prefix.
static const QStringList ALLOWED_DRIVES = {"c:", "z:"};

/// Filter out Wine/winetricks noise from process output.
/// Returns true if the line should be shown to the user.
static bool shouldShowLogLine(const QString& line)
{
  // Wine fixme/trace/err spam (e.g. "00d4:fixme:wineusb:query_id ...")
  static const QRegularExpression wineDebugRe(
      R"(^[0-9a-f]{4}:(?:fixme|trace|err):.*)");
  if (wineDebugRe.match(line).hasMatch())
    return false;

  // Wine diagnostic messages
  if (line.contains("ntsync:") || line.contains("winediag:") ||
      line.contains("pressure-vessel-wrap["))
    return false;

  // Winetricks execution spam
  if (line.startsWith("Executing ") || line.startsWith("Using winetricks ") ||
      line.startsWith("Executing w_do_call "))
    return false;

  // Blank lines and separator lines
  if (line.isEmpty() || line == "------------------------------------------------------")
    return false;

  return true;
}

/// Emit process output, splitting by line and filtering noise.
static void emitFilteredOutput(PrefixSetupRunner* self, const QByteArray& data)
{
  const QList<QByteArray> lines = data.split('\n');
  for (const QByteArray& raw : lines) {
    const QString line = QString::fromUtf8(raw).trimmed();
    if (shouldShowLogLine(line))
      emit self->logMessage(line);
  }
}

// Wine registry settings (.reg file content).
static const char* WINE_SETTINGS_REG = R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Wine\DllOverrides]
"dwrite.dll"="native,builtin"
"dwrite"="native,builtin"
"winmm.dll"="native,builtin"
"winmm"="native,builtin"
"version.dll"="native,builtin"
"version"="native,builtin"
"ArchiveXL.dll"="native,builtin"
"ArchiveXL"="native,builtin"
"Codeware.dll"="native,builtin"
"Codeware"="native,builtin"
"TweakXL.dll"="native,builtin"
"TweakXL"="native,builtin"
"input_loader.dll"="native,builtin"
"input_loader"="native,builtin"
"RED4ext.dll"="native,builtin"
"RED4ext"="native,builtin"
"mod_settings.dll"="native,builtin"
"mod_settings"="native,builtin"
"scc_lib.dll"="native,builtin"
"scc_lib"="native,builtin"
"dxgi.dll"="native,builtin"
"dxgi"="native,builtin"
"dbghelp.dll"="native,builtin"
"dbghelp"="native,builtin"
"d3d12.dll"="native,builtin"
"d3d12"="native,builtin"
"wininet.dll"="native,builtin"
"wininet"="native,builtin"
"winhttp.dll"="native,builtin"
"winhttp"="native,builtin"
"dinput.dll"="native,builtin"
"dinput8"="native,builtin"
"dinput8.dll"="native,builtin"
"hid"="native,builtin"
"hid.dll"="native,builtin"
"d3dcompiler_42"="native"
"d3dcompiler_43"="native"
"d3dcompiler_47"="native"
"d3dx9_24"="native"
"d3dx9_25"="native"
"d3dx9_26"="native"
"d3dx9_27"="native"
"d3dx9_28"="native"
"d3dx9_29"="native"
"d3dx9_30"="native"
"d3dx9_31"="native"
"d3dx9_32"="native"
"d3dx9_33"="native"
"d3dx9_34"="native"
"d3dx9_35"="native"
"d3dx9_36"="native"
"d3dx9_37"="native"
"d3dx9_38"="native"
"d3dx9_39"="native"
"d3dx9_40"="native"
"d3dx9_41"="native"
"d3dx9_42"="native"
"d3dx9_43"="native"
"d3dx10_33"="native"
"d3dx10_34"="native"
"d3dx10_35"="native"
"d3dx10_36"="native"
"d3dx10_37"="native"
"d3dx10_38"="native"
"d3dx10_39"="native"
"d3dx10_40"="native"
"d3dx10_41"="native"
"d3dx10_42"="native"
"d3dx10_43"="native"
"d3dx11_42"="native"
"d3dx11_43"="native"
; xinput left as builtin on Linux — native breaks controllers (SDL path).
"xaudio2_0"="native,builtin"
"xaudio2_1"="native,builtin"
"xaudio2_2"="native,builtin"
"xaudio2_3"="native,builtin"
"xaudio2_4"="native,builtin"
"xaudio2_5"="native,builtin"
"xaudio2_6"="native,builtin"
"xaudio2_7"="native,builtin"
"x3daudio1_0"="native,builtin"
"x3daudio1_1"="native,builtin"
"x3daudio1_2"="native,builtin"
"x3daudio1_3"="native,builtin"
"x3daudio1_4"="native,builtin"
"x3daudio1_5"="native,builtin"
"x3daudio1_6"="native,builtin"
"x3daudio1_7"="native,builtin"
"xapofx1_1"="native,builtin"
"xapofx1_2"="native,builtin"
"xapofx1_3"="native,builtin"
"xapofx1_4"="native,builtin"
"xapofx1_5"="native,builtin"
"xactengine2_0"="native,builtin"
"xactengine2_1"="native,builtin"
"xactengine2_2"="native,builtin"
"xactengine2_3"="native,builtin"
"xactengine2_4"="native,builtin"
"xactengine2_5"="native,builtin"
"xactengine2_6"="native,builtin"
"xactengine2_7"="native,builtin"
"xactengine2_8"="native,builtin"
"xactengine2_9"="native,builtin"
"xactengine2_10"="native,builtin"
"xactengine3_0"="native,builtin"
"xactengine3_1"="native,builtin"
"xactengine3_2"="native,builtin"
"xactengine3_3"="native,builtin"
"xactengine3_4"="native,builtin"
"xactengine3_5"="native,builtin"
"xactengine3_6"="native,builtin"
"xactengine3_7"="native,builtin"
"concrt140"="native,builtin"
"msvcp140"="native,builtin"
"msvcp140_1"="native,builtin"
"msvcp140_2"="native,builtin"
"msvcp140_atomic_wait"="native,builtin"
"msvcp140_codecvt_ids"="native,builtin"
"vcamp140"="native,builtin"
"vccorlib140"="native,builtin"
"vcomp140"="native,builtin"
"vcruntime140"="native,builtin"
"vcruntime140_1"="native,builtin"

[HKEY_CURRENT_USER\Software\Wine]
"ShowDotFiles"="Y"

[HKEY_CURRENT_USER\Control Panel\Desktop]
"FontSmoothing"="2"
"FontSmoothingGamma"=dword:00000578
"FontSmoothingOrientation"=dword:00000001
"FontSmoothingType"=dword:00000002

[HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers]
@="~ HIGHDPIAWARE"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\Pandora Behaviour Engine+.exe\X11 Driver]
"Decorated"="N"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\Vortex.exe\X11 Driver]
"Decorated"="N"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\SSEEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\SSEEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO4Edit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO4Edit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\TES4Edit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\TES4Edit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\SF1Edit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FNVEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FNVEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xFOEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xFOEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xSFEEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xSFEEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xTESEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xTESEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO3Edit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO3Edit64.exe]
"Version"="winxp"

; Native file browser integration
[HKEY_CLASSES_ROOT\Folder\shell\explore\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

[HKEY_CLASSES_ROOT\Directory\shell\explore\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

[HKEY_CLASSES_ROOT\Folder\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

[HKEY_CLASSES_ROOT\Directory\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

; Native text editor integration
[HKEY_CLASSES_ROOT\txtfile\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe \"%1\""

[HKEY_CLASSES_ROOT\inifile\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe \"%1\""

[HKEY_CLASSES_ROOT\.txt]
@="txtfile"

[HKEY_CLASSES_ROOT\.ini]
@="inifile"

[HKEY_CLASSES_ROOT\.cfg]
@="txtfile"

[HKEY_CLASSES_ROOT\.log]
@="txtfile"

[HKEY_CLASSES_ROOT\.xml]
@="txtfile"

[HKEY_CLASSES_ROOT\.json]
@="txtfile"

[HKEY_CLASSES_ROOT\.yml]
@="txtfile"

[HKEY_CLASSES_ROOT\.yaml]
@="txtfile"
)";

// ============================================================================
// Construction
// ============================================================================

PrefixSetupRunner::PrefixSetupRunner(const QString& prefixPath,
                                     const QString& protonPath,
                                     uint32_t appId,
                                     QObject* parent)
    : QObject(parent)
    , m_prefixPath(prefixPath)
    , m_protonPath(protonPath)
    , m_appId(appId)
{
  m_wineBin       = findWineBinary();
  m_wineserverBin = findWineserverBinary();
  m_slrRunScript  = detectSLRRunScript();

  buildStepList();
}

// ============================================================================
// Step list construction
// ============================================================================

void PrefixSetupRunner::buildStepList()
{
  m_steps.clear();
  m_stepFunctions.clear();

  auto addStep = [this](const QString& id, const QString& name,
                        std::function<bool()> fn) {
    m_steps.append({.id=id, .displayName=name, .status=SetupStep::Pending, .errorMessage={}});
    m_stepFunctions.append(std::move(fn));
  };

  addStep("proton_init", "Initialize Wine Prefix",
          [this] { return stepProtonInit(); });

  addStep("drive_cleanup", "Clean Up Drive Letters",
          [this] { return stepDriveCleanup(); });

  // DirectX DLL extraction (cab-based, no Wine needed for most).
  addStep("directx_runtime", "DirectX Runtimes",
          [this] { return stepDirectXRuntime(); });

  // Runtime installers (run via Wine).
  addStep("vcrun2022", "Visual C++ 2022",
          [this] { return stepVcrun2022(); });
  addStep("dotnet_runtimes", ".NET Runtimes (6-9)",
          [this] { return stepDotNetRuntimes(); });
  addStep("dotnet10_sdk", ".NET 10 SDK",
          [this] { return stepDotNetInstall(DOTNET10_SDK_URL, ".NET 10 SDK"); });
  addStep("nuget_signature_policy", "NuGet Signature Policy",
          [this] { return stepNuGetSignaturePolicy(); });

  addStep("game_detection", "Auto-Detect Games",
          [this] { return stepGameDetection(); });

  addStep("wine_registry", "Wine Registry Settings",
          [this] { return stepWineRegistry(); });

  addStep("win11_mode", "Windows 11 Mode",
          [this] { return stepWin11Mode(); });

  addStep("post_setup", "Post-Setup (symlinks, dxvk)",
          [this] { return stepPostSetup(); });
}

// ============================================================================
// Execution
// ============================================================================

void PrefixSetupRunner::start()
{
  m_cancelled.storeRelease(0);

  // Ensure tools are available before starting steps.
  // cabextract is needed for DirectX cab extraction and vcrun2022 workaround.
  // winetricks is only needed for win11 mode (non-fatal if missing).
  if (!ensureCabextract()) {
    emit finished(false);
    return;
  }
  ensureWinetricks();  // Best-effort for win11 step.

  const int total = m_steps.size();
  bool allOk = true;

  for (int i = 0; i < total; ++i) {
    if (isCancelled()) {
      emit logMessage("Cancelled by user.");
      allOk = false;
      break;
    }

    if (m_steps[i].status == SetupStep::Succeeded)
      continue;

    const bool stepOk = runStep(i);
    allOk             = stepOk && allOk;
    emit progressChanged(static_cast<float>(i + 1) / total);

    // The prefix-init step is a hard prerequisite for everything that
    // follows. If it fails (e.g. broken Proton install), running downstream
    // steps just produces a cascade of misleading "version mismatch" errors
    // against a half-initialized prefix. Surface the real error and stop.
    if (!stepOk && m_steps[i].id == "proton_init") {
      emit logMessage(
          "Prefix initialization failed — skipping remaining setup steps. "
          "Fix the Proton installation and retry.");
      break;
    }
  }

  emit finished(allOk);
}

void PrefixSetupRunner::retryFailed()
{
  m_cancelled.storeRelease(0);

  const int total = m_steps.size();
  bool allOk = true;

  for (int i = 0; i < total; ++i) {
    if (isCancelled()) break;

    if (m_steps[i].status != SetupStep::Failed)
      continue;

    allOk = runStep(i) && allOk;
  }

  emit finished(allOk);
}

void PrefixSetupRunner::retryStep(int index)
{
  if (index < 0 || index >= m_steps.size()) return;
  m_cancelled.storeRelease(0);

  runStep(index);

  // Check if everything is now good.
  bool allOk = true;
  for (const auto& s : m_steps) {
    if (s.status == SetupStep::Failed) { allOk = false; break; }
  }
  emit finished(allOk);
}

bool PrefixSetupRunner::runStep(int index)
{
  m_steps[index].status       = SetupStep::Running;
  m_steps[index].errorMessage.clear();
  m_currentStepIndex = index;
  emit stepStarted(index);

  const bool ok = m_stepFunctions[index]();
  m_currentStepIndex = -1;

  m_steps[index].status = ok ? SetupStep::Succeeded : SetupStep::Failed;
  if (!ok && m_steps[index].errorMessage.isEmpty())
    m_steps[index].errorMessage = "Step failed (see log for details)";

  emit stepFinished(index, ok, m_steps[index].errorMessage);
  return ok;
}

// ============================================================================
// Process execution
// ============================================================================

QProcess* PrefixSetupRunner::buildWrappedProcess(
    const QString& exe,
    const QMap<QString, QString>& extraEnv)
{
  auto* proc = new QProcess(this);

  // Start from the system environment and clean Fluorine bundling vars.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Keep installer scratch data on the user's home filesystem.  /tmp and
  // XDG_RUNTIME_DIR are frequently small tmpfs mounts, which can make large
  // Microsoft installers report ERROR_DISK_FULL even when the prefix has
  // plenty of free space.
  const QString tmpDir = fluorineTmpDir();
  QDir().mkpath(tmpDir);
  QString wineTmpDir = QStringLiteral("Z:") + tmpDir;
  wineTmpDir.replace(QLatin1Char('/'), QLatin1Char('\\'));
  env.insert(QStringLiteral("TMPDIR"), tmpDir);
  env.insert(QStringLiteral("TMP"), wineTmpDir);
  env.insert(QStringLiteral("TEMP"), wineTmpDir);

  // Remove Fluorine vars that can confuse Wine.
  for (const char* var : {"QT_QPA_PLATFORM_PLUGIN_PATH", "MO2_PLUGINS_DIR",
       "MO2_LIBS_DIR", "MO2_PYTHON_DIR", "MO2_BASE_DIR"}) {
    env.remove(var);
  }

  // Restore pre-launcher environment if available.
  auto restoreOrStrip = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) env.remove(var);
      else env.insert(var, orig);
      env.remove(origVar);
    }
  };
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreOrStrip("LD_PRELOAD",      "FLUORINE_ORIG_LD_PRELOAD");
  restoreOrStrip("PATH",            "FLUORINE_ORIG_PATH");
  restoreOrStrip("XDG_DATA_DIRS",   "FLUORINE_ORIG_XDG_DATA_DIRS");
  restoreOrStrip("QT_PLUGIN_PATH",  "FLUORINE_ORIG_QT_PLUGIN_PATH");

  // Expose the injected xrandr (steamrt4 ships without it) so protonfixes
  // and Proton-GE init scripts can find it. Pressure-vessel forces PATH
  // inside the container, so we prepend the xrandr dir on the HOST PATH
  // and also pass it through --filesystem below.
  const QString xrandrDir =
      QDir::homePath() + "/.local/share/fluorine/steamrt/xrandr-bin";
  if (QDir(xrandrDir).exists()) {
    const QString existing = env.value("PATH");
    env.insert("PATH", existing.isEmpty()
                           ? xrandrDir
                           : xrandrDir + QLatin1Char(':') + existing);
  }

  // Apply caller-provided env vars.
  for (auto it = extraEnv.begin(); it != extraEnv.end(); ++it) {
    env.insert(it.key(), it.value());
  }

  proc->setProcessEnvironment(env);

  // Wrap in SLR if available.
  if (!m_slrRunScript.isEmpty()) {
    QStringList slrArgs;

    // Expose the executable's parent directory.
    const QString exeDir = QFileInfo(exe).absolutePath();
    if (!exeDir.isEmpty() && QDir(exeDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(exeDir);

    // Expose the Wine prefix.
    if (!m_prefixPath.isEmpty())
      slrArgs << QStringLiteral("--filesystem=%1").arg(m_prefixPath);

    // Expose Proton directory.
    if (!m_protonPath.isEmpty())
      slrArgs << QStringLiteral("--filesystem=%1").arg(m_protonPath);

    // Expose fluorine bin dir (cabextract, winetricks).
    const QString binDir = fluorineBinDir();
    if (QDir(binDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(binDir);

    // Expose cache dir.
    const QString cacheDir = fluorineCacheDir();
    if (QDir(cacheDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(cacheDir);

    // TMPDIR is redirected here above, so it must also be visible inside the
    // Steam Linux Runtime container at the same absolute path.
    if (QDir(tmpDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(tmpDir);

    // Expose the injected xrandr bin dir so Proton-GE's protonfixes can
    // invoke it during wineboot -u. Without this the container's PATH
    // (forced to /usr/bin:/bin) has no xrandr and init fails on some
    // modern Proton builds. See issue #49.
    if (QDir(xrandrDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(xrandrDir);

    // Pressure-vessel resets PATH inside the container. Wrap the exec
    // through /usr/bin/env to inject the xrandr dir back into the
    // container's PATH.
    if (QDir(xrandrDir).exists()) {
      slrArgs << "--" << QStringLiteral("/usr/bin/env")
              << QStringLiteral("PATH=%1:/usr/bin:/bin").arg(xrandrDir);
      slrArgs << exe;
    } else {
      slrArgs << "--" << exe;
    }

    proc->setProgram(m_slrRunScript);
    proc->setArguments(slrArgs);
  } else {
    proc->setProgram(exe);
  }

  return proc;
}

int PrefixSetupRunner::runProcess(const QString& exe,
                                  const QStringList& args,
                                  const QMap<QString, QString>& extraEnv,
                                  int timeoutMs,
                                  QByteArray* captured)
{
  QProcess* proc = buildWrappedProcess(exe, extraEnv);

  // Append the actual arguments after the SLR wrapper arguments.
  QStringList fullArgs = proc->arguments();
  fullArgs.append(args);
  proc->setArguments(fullArgs);

  proc->setProcessChannelMode(QProcess::MergedChannels);
  proc->start();

  // Poll for output and cancellation.
  while (proc->state() != QProcess::NotRunning) {
    proc->waitForReadyRead(250);

    if (proc->canReadLine() || proc->bytesAvailable() > 0) {
      const QByteArray data = proc->readAll();
      if (!data.isEmpty()) {
        if (captured) captured->append(data);
        emitFilteredOutput(this, data);
      }
    }

    if (isCancelled()) {
      proc->kill();
      proc->waitForFinished(5000);
      proc->deleteLater();
      return -1;
    }
  }

  const QByteArray remaining = proc->readAll();
  if (!remaining.isEmpty()) {
    if (captured) captured->append(remaining);
    emitFilteredOutput(this, remaining);
  }

  const int exitCode = proc->exitCode();
  proc->deleteLater();
  return exitCode;
}

int PrefixSetupRunner::runHostProcess(const QString& exe,
                                      const QStringList& args,
                                      int timeoutMs)
{
  QProcess proc;
  proc.setProgram(exe);
  proc.setArguments(args);
  proc.setProcessChannelMode(QProcess::MergedChannels);

  // Clean Fluorine env so host tools find their system libraries.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreOrStrip = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) env.remove(var);
      else env.insert(var, orig);
      env.remove(origVar);
    }
  };
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreOrStrip("LD_PRELOAD",      "FLUORINE_ORIG_LD_PRELOAD");
  restoreOrStrip("PATH",            "FLUORINE_ORIG_PATH");
  proc.setProcessEnvironment(env);

  proc.start();

  while (proc.state() != QProcess::NotRunning) {
    proc.waitForReadyRead(250);
    if (proc.bytesAvailable() > 0)
      emitFilteredOutput(this, proc.readAll());
    if (isCancelled()) {
      proc.kill();
      proc.waitForFinished(5000);
      return -1;
    }
  }

  const QByteArray remaining = proc.readAll();
  if (!remaining.isEmpty())
    emitFilteredOutput(this, remaining);

  return proc.exitCode();
}

int PrefixSetupRunner::runHostProcessWithEnv(const QString& exe,
                                             const QStringList& args,
                                             const QMap<QString, QString>& extraEnv,
                                             int timeoutMs)
{
  QProcess proc;
  proc.setProgram(exe);
  proc.setArguments(args);
  proc.setProcessChannelMode(QProcess::MergedChannels);

  // Start from cleaned host environment, then apply caller's env vars.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreVar = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) env.remove(var);
      else env.insert(var, orig);
      env.remove(origVar);
    }
  };
  restoreVar("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreVar("LD_PRELOAD",      "FLUORINE_ORIG_LD_PRELOAD");

  for (auto it = extraEnv.begin(); it != extraEnv.end(); ++it)
    env.insert(it.key(), it.value());

  proc.setProcessEnvironment(env);
  proc.start();

  while (proc.state() != QProcess::NotRunning) {
    proc.waitForReadyRead(250);
    if (proc.bytesAvailable() > 0)
      emitFilteredOutput(this, proc.readAll());
    if (isCancelled()) {
      proc.kill();
      proc.waitForFinished(5000);
      return -1;
    }
  }

  const QByteArray remaining = proc.readAll();
  if (!remaining.isEmpty())
    emitFilteredOutput(this, remaining);

  return proc.exitCode();
}

// ============================================================================
// Step implementations
// ============================================================================

bool PrefixSetupRunner::stepProtonInit()
{
  const QString protonScript = findProtonScript();
  if (protonScript.isEmpty()) {
    currentStep().errorMessage = "Proton wrapper script not found";
    return false;
  }

  // Kill any stale wineboot/wineserver/pv-adverb processes bound to this
  // prefix. If a previous run was aborted mid-init, those processes hold
  // registry/filesystem locks and any new wineboot -u deadlocks waiting
  // for them. Nothing cleans them up automatically.
  killStalePrefixProcesses();

  if (!ensureSLRRunScript()) {
    return false;
  }

  // Proton-GE invokes `xrandr` during protonfixes at wineboot time. Always
  // ensure Fluorine's injected helper exists after SLR is available, then the
  // wrapper below exposes that exact directory and prepends it to PATH.
  emit logMessage("Ensuring xrandr helper is available...");
  if (!ensureXrandrInstalled(
          {}, [this](const QString& msg) { emit logMessage(msg); })) {
    currentStep().errorMessage = "Failed to install xrandr helper";
    return false;
  }

  const QString steamPath = detectSteamPath();

  // The compatdata path is the PARENT of the pfx directory.
  const QString compatDataPath = QDir(m_prefixPath).filePath("..");
  const QString cleanCompat    = QDir::cleanPath(compatDataPath);

  QMap<QString, QString> env;
  env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = steamPath;
  env["STEAM_COMPAT_DATA_PATH"]           = cleanCompat;
  env["SteamAppId"]                       = QString::number(m_appId);
  env["SteamGameId"]                      = QString::number(m_appId);
  // Keep DISPLAY/WAYLAND_DISPLAY from the host: Proton-GE protonfixes runs
  // `xrandr` during wineboot -u to detect monitors, and xrandr exits 1 if
  // it can't open a display, which cascades to a failed prefix init on
  // newer Proton-GE builds.
  env["WINEDLLOVERRIDES"] = "msdia80.dll=n;conhost.exe=d;cmd.exe=d";
  // ntsync on kernel 7.0+ can deadlock wineboot -u during prefix init
  // under Proton 11 (wineboot blocks in ntsync_char_ioctl forever). Force
  // the older fsync/esync fallback for the init phase — once the prefix
  // is created, regular game launches can use whatever sync they want.
  env["WINE_DISABLE_FAST_SYNC"] = "1";
  env["PROTON_NO_NTSYNC"]       = "1";
  env["WINENTSYNC"]             = "0";

  // waitforexitandrun (not "run") is required for Proton 11+ which uses
  // use_sessions=1 — a plain "run" forks into a session manager that never
  // exits, hanging prefix init.
  QByteArray protonOutput;
  emit logMessage("Initializing Wine prefix with Proton...");
  const int rc = runProcess(protonScript,
                            {"waitforexitandrun", "wineboot", "-u"},
                            env, -1, &protonOutput);

  if (rc != 0) {
    // Detect a broken Proton install: setup_prefix copies DLLs from Proton's
    // bundled default_pfx template, so a FileNotFoundError there means the
    // Proton distribution itself is missing files — not a Fluorine issue.
    const QByteArray out = protonOutput;
    if (out.contains("FileNotFoundError") &&
        out.contains("default_pfx/drive_c")) {
      int start = out.indexOf("default_pfx/drive_c");
      int end   = out.indexOf('\'', start);
      if (end < 0) end = out.indexOf('"', start);
      const QString missing =
          (end > start) ? QString::fromUtf8(out.mid(start, end - start))
                        : QStringLiteral("default_pfx/drive_c/...");
      currentStep().errorMessage =
          QStringLiteral(
              "Proton install is incomplete: missing '%1'. "
              "Reinstall or verify your Proton build (e.g. GE-Proton) — "
              "this is not a Fluorine bug.")
              .arg(missing);
    } else {
      currentStep().errorMessage =
          QStringLiteral("proton wineboot failed (exit code %1)").arg(rc);
    }
    return false;
  }

  // Wait briefly for files to settle.
  QThread::sleep(2);

  if (!QDir(m_prefixPath).exists()) {
    currentStep().errorMessage = "Prefix directory not created after wineboot";
    return false;
  }

  return true;
}

bool PrefixSetupRunner::stepDriveCleanup()
{
  const QString dosdevices = m_prefixPath + "/dosdevices";
  if (!QDir(dosdevices).exists()) {
    emit logMessage("dosdevices not found, skipping drive cleanup");
    return true;
  }

  emit logMessage("Removing unwanted drive letters...");

  QStringList removed;
  const QDir dir(dosdevices);
  for (const QString& entry : dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
    const QString lower = entry.toLower();
    if (lower.length() != 2 || !lower.endsWith(':'))
      continue;
    if (!lower.at(0).isLetter())
      continue;
    if (ALLOWED_DRIVES.contains(lower))
      continue;

    QFile::remove(dir.filePath(entry));
    removed << entry.toUpper();
  }

  if (!removed.isEmpty()) {
    emit logMessage(QStringLiteral("Removed drive symlinks: %1").arg(removed.join(", ")));

    // Clean registry entries for removed drives.
    const QString tmpDir = fluorineTmpDir();
    QDir().mkpath(tmpDir);

    QString regContent = "Windows Registry Editor Version 5.00\n\n";
    for (const QString& drive : removed) {
      regContent += QStringLiteral(
          "[HKEY_LOCAL_MACHINE\\Software\\Wine\\Drives]\n\"%1\"=-\n\n").arg(drive);
    }

    const QString regFile = tmpDir + "/drive_cleanup.reg";
    QFile f(regFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      f.write(regContent.toUtf8());
      f.close();

      QMap<QString, QString> env = baseWineEnv();
      env["WINEDLLOVERRIDES"] = "mshtml=d";
      env["PROTON_USE_XALIA"] = "0";

      runProcess(m_wineBin, {"regedit", regFile}, env);
      QFile::remove(regFile);
    }
  }

  return true;
}

// ============================================================================
// DirectX cab extraction helpers
// ============================================================================

bool PrefixSetupRunner::ensureDirectXRedist(QString& redistPath)
{
  const QString cacheDir = fluorineCacheDir() + "/directx9";
  QDir().mkpath(cacheDir);
  redistPath = cacheDir + "/directx_Jun2010_redist.exe";

  if (!QFileInfo::exists(redistPath)) {
    emit logMessage("Downloading DirectX June 2010 redistributable...");
    return downloadAndVerifyAny(DIRECTX_JUN2010_URLS, redistPath,
                                DIRECTX_JUN2010_SHA256);
  }

  const QString cachedSha = fileSha256(redistPath);
  if (!DIRECTX_JUN2010_SHA256.contains(cachedSha, Qt::CaseInsensitive)) {
    emit logMessage("Cached redist has bad checksum, re-downloading...");
    QFile::remove(redistPath);
    return downloadAndVerifyAny(DIRECTX_JUN2010_URLS, redistPath,
                                DIRECTX_JUN2010_SHA256);
  }

  return true;
}

bool PrefixSetupRunner::extractFromRedist(const QString& redistPath,
                                          const QString& cabFilter,
                                          const QString& dllFilter,
                                          const QString& destDir)
{
  const QString tmpDir = fluorineTmpDir() + "/dxextract";
  QDir().mkpath(tmpDir);

  // Clean tmp dir.
  QDir tmp(tmpDir);
  for (const QString& f : tmp.entryList(QDir::Files))
    tmp.remove(f);

  const QString cabextractBin = fluorineBinDir() + "/cabextract";

  // Stage 1: extract inner cab(s) matching filter from the outer redist.
  int rc = runHostProcess(cabextractBin,
      {"-d", tmpDir, "-L", "-F", cabFilter, redistPath});
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("cabextract failed (filter: %1, exit %2)").arg(cabFilter).arg(rc);
    return false;
  }

  // Stage 2: extract DLL(s) from each inner cab.
  const QStringList cabs = QDir(tmpDir).entryList({"*.cab"}, QDir::Files);
  if (cabs.isEmpty()) {
    currentStep().errorMessage =
        QStringLiteral("No inner cab found for filter: %1").arg(cabFilter);
    return false;
  }

  for (const QString& cab : cabs) {
    rc = runHostProcess(cabextractBin,
        {"-d", destDir, "-L", "-F", dllFilter, tmpDir + "/" + cab});
    if (rc != 0) {
      currentStep().errorMessage =
          QStringLiteral("cabextract inner failed (filter: %1, exit %2)").arg(dllFilter).arg(rc);
      return false;
    }
  }

  // Clean up tmp.
  QDir(tmpDir).removeRecursively();
  return true;
}

// ============================================================================
// DirectX DLL steps
// ============================================================================

bool PrefixSetupRunner::stepD3DCompiler47()
{
  emit logMessage("Installing d3dcompiler_47...");

  const QString cacheDir = fluorineCacheDir() + "/d3dcompiler_47";
  QDir().mkpath(cacheDir);

  // On a win64 prefix (matching real Windows):
  //   system32  = 64-bit DLLs
  //   syswow64  = 32-bit DLLs
  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  // 32-bit DLL → syswow64
  {
    const QString cached = cacheDir + "/d3dcompiler_47_32.dll";
    if (!QFileInfo::exists(cached)) {
      emit logMessage("Downloading d3dcompiler_47 (32-bit)...");
      if (!downloadAndVerify(D3DCOMPILER_47_32_URL, cached, D3DCOMPILER_47_32_SHA256))
        return false;
    }
    const QString dest = dllDir32 + "/d3dcompiler_47.dll";
    QFile::remove(dest);
    if (!QFile::copy(cached, dest)) {
      currentStep().errorMessage = "Failed to copy d3dcompiler_47.dll to syswow64";
      return false;
    }
  }

  // 64-bit DLL → system32
  {
    const QString cached = cacheDir + "/d3dcompiler_47.dll";
    if (!QFileInfo::exists(cached)) {
      emit logMessage("Downloading d3dcompiler_47 (64-bit)...");
      if (!downloadAndVerify(D3DCOMPILER_47_64_URL, cached, D3DCOMPILER_47_64_SHA256))
        return false;
    }
    const QString dest = dllDir64 + "/d3dcompiler_47.dll";
    QFile::remove(dest);
    if (!QFile::copy(cached, dest)) {
      currentStep().errorMessage = "Failed to copy d3dcompiler_47.dll to system32";
      return false;
    }
  }

  emit logMessage("d3dcompiler_47 installed");
  return true;
}

bool PrefixSetupRunner::stepDirectXRuntime()
{
  emit logMessage("Installing DirectX runtimes...");

  if (!applyDllOverrides(DIRECTX_NATIVE_DLLS, DIRECTX_NATIVE_BUILTIN_DLLS))
    return false;

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  // d3dcompiler_47: prebuilt DLLs from Mozilla fxc2 (not in the June 2010 redist).
  if (!stepD3DCompiler47())
    return false;

  // Everything else comes from the DirectX June 2010 redist.
  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  // All single-DLL extractions: {cabFilter, dllFilter, displayName}
  struct DllEntry { const char* cabFilter; const char* dllFilter; const char* name; };
  static const DllEntry singleDlls[] = {
    {.cabFilter="*d3dcompiler_42*", .dllFilter="d3dcompiler_42.dll", .name="d3dcompiler_42"},
    {.cabFilter="*d3dcompiler_43*", .dllFilter="d3dcompiler_43.dll", .name="d3dcompiler_43"},
    {.cabFilter="*d3dx11_42*",      .dllFilter="d3dx11_42.dll",      .name="d3dx11_42"},
    {.cabFilter="*d3dx11_43*",      .dllFilter="d3dx11_43.dll",      .name="d3dx11_43"},
  };

  // Multi-DLL extractions (wildcards): {cabFilter, dllFilter, displayName}
  static const DllEntry multiDlls[] = {
    {.cabFilter="*d3dx9*",    .dllFilter="d3dx9*.dll",  .name="d3dx9"},
    {.cabFilter="*d3dx10*",   .dllFilter="d3dx10*.dll", .name="d3dx10"},
    {.cabFilter="*_xinput_*", .dllFilter="xinput*.dll", .name="xinput"},
  };

  // Extract single DLLs (both arches).
  for (const auto& e : singleDlls) {
    if (isCancelled()) return false;
    emit logMessage(QStringLiteral("Extracting %1...").arg(e.name));
    extractFromRedist(redistPath, QStringLiteral("%1x86*").arg(e.cabFilter), e.dllFilter, dllDir32);
    extractFromRedist(redistPath, QStringLiteral("%1x64*").arg(e.cabFilter), e.dllFilter, dllDir64);
  }

  // Extract multi-DLLs (both arches).
  for (const auto& e : multiDlls) {
    if (isCancelled()) return false;
    emit logMessage(QStringLiteral("Extracting %1...").arg(e.name));
    extractFromRedist(redistPath, QStringLiteral("%1x86*").arg(e.cabFilter), e.dllFilter, dllDir32);
    extractFromRedist(redistPath, QStringLiteral("%1x64*").arg(e.cabFilter), e.dllFilter, dllDir64);
  }

  // XACT / XAudio / X3DAudio / XAPOFX — 32-bit.
  if (!isCancelled()) {
    emit logMessage("Extracting XACT (32-bit)...");
    for (const char* cabPat : {"*_xact_*x86*", "*_x3daudio_*x86*", "*_xaudio_*x86*"})
      for (const char* dllPat : {"xactengine*.dll", "xaudio*.dll", "x3daudio*.dll", "xapofx*.dll"})
        extractFromRedist(redistPath, cabPat, dllPat, dllDir32);
  }

  // XACT — 64-bit.
  if (!isCancelled()) {
    emit logMessage("Extracting XACT (64-bit)...");
    for (const char* cabPat : {"*_xact_*x64*", "*_x3daudio_*x64*", "*_xaudio_*x64*"})
      for (const char* dllPat : {"xactengine*.dll", "xaudio*.dll", "x3daudio*.dll", "xapofx*.dll"})
        extractFromRedist(redistPath, cabPat, dllPat, dllDir64);
  }

  // Batch-register XACT DLLs via regsvr32.
  auto collectRegDlls = [](const QString& dir) -> QStringList {
    QStringList dlls;
    dlls.append(QDir(dir).entryList({"xactengine*.dll"}, QDir::Files));
    for (int i = 0; i <= 7; ++i) {
      const QString dll = QStringLiteral("xaudio2_%1.dll").arg(i);
      if (QFileInfo::exists(dir + "/" + dll))
        dlls.append(dll);
    }
    return dlls;
  };

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = makeDllOverrideEnv(
      "mshtml=d", DIRECTX_NATIVE_DLLS, DIRECTX_NATIVE_BUILTIN_DLLS);

  // 32-bit registration.
  QStringList reg32 = collectRegDlls(dllDir32);
  if (!reg32.isEmpty()) {
    emit logMessage(QStringLiteral("Registering %1 32-bit DLLs...").arg(reg32.size()));
    QStringList args = {"regsvr32", "/S"};
    args.append(reg32);
    runProcess(m_wineBin, args, env);
  }

  // 64-bit registration.
  QStringList reg64 = collectRegDlls(dllDir64);
  if (!reg64.isEmpty()) {
    emit logMessage(QStringLiteral("Registering %1 64-bit DLLs...").arg(reg64.size()));
    QStringList args = {"regsvr32", "/S"};
    args.append(reg64);
    runProcess(m_wineBin, args, env);
  }

  emit logMessage("DirectX runtimes installed");
  return true;
}

// ============================================================================
// Runtime installer steps
// ============================================================================

bool PrefixSetupRunner::stepVcrun2022()
{
  emit logMessage("Installing Visual C++ 2022...");

  if (!applyDllOverrides({}, VCRUN_DLLS))
    return false;

  const QString cacheDir = fluorineCacheDir() + "/vcrun2022";
  const QString tmpDir   = fluorineTmpDir() + "/vcrun2022";
  QDir().mkpath(cacheDir);
  QDir().mkpath(tmpDir);

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";
  const QString cabextractBin = fluorineBinDir() + "/cabextract";

  // Download x86 installer.
  const QString x86Path = cacheDir + "/vc_redist.x86.exe";
  if (!QFileInfo::exists(x86Path)) {
    emit logMessage("Downloading vc_redist.x86.exe...");
    if (!downloadRuntimeInstaller(VCRUN2022_X86_URL, x86Path,
                                  "vc_redist.x86.exe",
                                  VCRUN2022_X86_SHA256)) {
      return false;
    }
  } else if (!downloadRuntimeInstaller(VCRUN2022_X86_URL, x86Path,
                                       "vc_redist.x86.exe",
                                       VCRUN2022_X86_SHA256)) {
    return false;
  }

  // Wine bug #57518 workaround: manually extract msvcp140.dll before running
  // the installer, because the installer refuses to replace the builtin
  // (builtin version number is higher).
  emit logMessage("Extracting msvcp140.dll (32-bit)...");
  int rc = runHostProcess(cabextractBin,
      {"--directory=" + tmpDir + "/win32", x86Path, "-F", "a10"});
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("vc_redist.x86.exe cab extraction failed (exit code %1)").arg(rc);
    return false;
  }
  rc = runHostProcess(cabextractBin,
      {"--directory=" + dllDir32, tmpDir + "/win32/a10", "-F", "msvcp140.dll"});
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("msvcp140.dll x86 extraction failed (exit code %1)").arg(rc);
    return false;
  }

  // Run 32-bit installer.
  emit logMessage("Running vc_redist.x86.exe...");
  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = makeDllOverrideEnv("mshtml=d", {}, VCRUN_DLLS);

  rc = runProcess(m_wineBin, {x86Path, "/install", "/quiet", "/norestart"}, env);
  if (!isMicrosoftInstallerSuccess(rc)) {
    currentStep().errorMessage =
        QStringLiteral("vc_redist.x86.exe failed (%1, SHA256 %2)")
            .arg(describeInstallerExitCode(rc))
            .arg(fileSha256(x86Path));
    return false;
  } else if (rc != 0) {
    emit logMessage(QStringLiteral("vc_redist.x86.exe returned nonfatal exit code %1").arg(rc));
  }

  // Download and run x64 installer.
  const QString x64Path = cacheDir + "/vc_redist.x64.exe";
  if (!QFileInfo::exists(x64Path)) {
    emit logMessage("Downloading vc_redist.x64.exe...");
    if (!downloadRuntimeInstaller(VCRUN2022_X64_URL, x64Path,
                                  "vc_redist.x64.exe",
                                  VCRUN2022_X64_SHA256)) {
      return false;
    }
  } else if (!downloadRuntimeInstaller(VCRUN2022_X64_URL, x64Path,
                                       "vc_redist.x64.exe",
                                       VCRUN2022_X64_SHA256)) {
    return false;
  }

  emit logMessage("Extracting msvcp140.dll (64-bit)...");
  rc = runHostProcess(cabextractBin,
      {"--directory=" + tmpDir + "/win64", x64Path, "-F", "a12"});
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("vc_redist.x64.exe cab extraction failed (exit code %1)").arg(rc);
    return false;
  }
  rc = runHostProcess(cabextractBin,
      {"--directory=" + dllDir64, tmpDir + "/win64/a12", "-F", "msvcp140.dll"});
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("msvcp140.dll x64 extraction failed (exit code %1)").arg(rc);
    return false;
  }

  emit logMessage("Running vc_redist.x64.exe...");
  rc = runProcess(m_wineBin, {x64Path, "/install", "/quiet", "/norestart"}, env);
  if (!isMicrosoftInstallerSuccess(rc)) {
    currentStep().errorMessage =
        QStringLiteral("vc_redist.x64.exe failed (%1, SHA256 %2)")
            .arg(describeInstallerExitCode(rc))
            .arg(fileSha256(x64Path));
    return false;
  } else if (rc != 0) {
    emit logMessage(QStringLiteral("vc_redist.x64.exe returned nonfatal exit code %1").arg(rc));
  }

  QDir(tmpDir).removeRecursively();
  emit logMessage("Visual C++ 2022 installed");
  return true;
}

bool PrefixSetupRunner::stepDotNetRuntimes()
{
  emit logMessage("Installing .NET Runtimes (6-9)...");

  struct RuntimePair {
    const char* url32;
    const char* url64;
    const char* name;
    const QStringList* sha32;
    const QStringList* sha64;
  };
  static const RuntimePair runtimes[] = {
    {.url32=DOTNET6_X86_URL, .url64=DOTNET6_X64_URL, .name=".NET 6",
     .sha32=&DOTNET6_X86_SHA256, .sha64=&DOTNET6_X64_SHA256},
    {.url32=DOTNET7_X86_URL, .url64=DOTNET7_X64_URL, .name=".NET 7",
     .sha32=&DOTNET7_X86_SHA256, .sha64=&DOTNET7_X64_SHA256},
    {.url32=DOTNET8_X86_URL, .url64=DOTNET8_X64_URL, .name=".NET 8",
     .sha32=&DOTNET8_X86_SHA256, .sha64=&DOTNET8_X64_SHA256},
    {.url32=DOTNET9_X86_URL, .url64=DOTNET9_X64_URL, .name=".NET 9",
     .sha32=&DOTNET9_X86_SHA256, .sha64=&DOTNET9_X64_SHA256},
  };

  for (const auto& rt : runtimes) {
    if (isCancelled()) return false;
    if (!stepDotNetInstallPair(rt.url32, rt.url64, rt.name,
                               *rt.sha32, *rt.sha64))
      return false;
  }

  emit logMessage(".NET Runtimes installed");
  return true;
}

bool PrefixSetupRunner::stepDotNetInstallPair(const QString& url32, const QString& url64,
                                              const QString& name,
                                              const QStringList& knownSha25632,
                                              const QStringList& knownSha25664)
{
  const QString cacheDir = fluorineCacheDir();
  QDir().mkpath(cacheDir);

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  // Install 32-bit runtime.
  {
    const QString filename = QUrl(url32).fileName();
    const QString path     = cacheDir + "/" + filename;

    if (!QFileInfo::exists(path)) {
      emit logMessage(QStringLiteral("Downloading %1 (32-bit)...").arg(name));
      if (!downloadRuntimeInstaller(url32, path,
                                    QStringLiteral("%1 x86").arg(name),
                                    knownSha25632)) {
        return false;
      }
    } else if (!downloadRuntimeInstaller(url32, path,
                                         QStringLiteral("%1 x86").arg(name),
                                         knownSha25632)) {
      return false;
    }

    emit logMessage(QStringLiteral("Installing %1 (32-bit)...").arg(name));
    QByteArray installerOutput;
    QString installerLogPath;
    const int rc = runDotNetInstaller(path, QStringLiteral("%1 x86").arg(name),
                                      env, &installerOutput, &installerLogPath);
    if (!isMicrosoftInstallerSuccess(rc)) {
      const QString diagnosticsPath = reportInstallerFailure(
          QStringLiteral("%1 x86").arg(name), path, rc, installerOutput,
          installerLogPath);
      currentStep().errorMessage =
          QStringLiteral("%1 x86 installer failed (%2)")
              .arg(name, describeInstallerExitCode(rc));
      if (!diagnosticsPath.isEmpty())
        currentStep().errorMessage +=
            QStringLiteral("; diagnostics: %1").arg(diagnosticsPath);
      return false;
    } else if (rc != 0) {
      emit logMessage(QStringLiteral("%1 x86 installer returned nonfatal exit code %2")
                          .arg(name)
                          .arg(rc));
    }
    QFile::remove(installerLogPath);
  }

  // Install 64-bit runtime.
  {
    const QString filename = QUrl(url64).fileName();
    const QString path     = cacheDir + "/" + filename;

    if (!QFileInfo::exists(path)) {
      emit logMessage(QStringLiteral("Downloading %1 (64-bit)...").arg(name));
      if (!downloadRuntimeInstaller(url64, path,
                                    QStringLiteral("%1 x64").arg(name),
                                    knownSha25664)) {
        return false;
      }
    } else if (!downloadRuntimeInstaller(url64, path,
                                         QStringLiteral("%1 x64").arg(name),
                                         knownSha25664)) {
      return false;
    }

    emit logMessage(QStringLiteral("Installing %1 (64-bit)...").arg(name));
    QByteArray installerOutput;
    QString installerLogPath;
    const int rc = runDotNetInstaller(path, QStringLiteral("%1 x64").arg(name),
                                      env, &installerOutput, &installerLogPath);
    if (!isMicrosoftInstallerSuccess(rc)) {
      const QString diagnosticsPath = reportInstallerFailure(
          QStringLiteral("%1 x64").arg(name), path, rc, installerOutput,
          installerLogPath);
      currentStep().errorMessage =
          QStringLiteral("%1 x64 installer failed (%2)")
              .arg(name, describeInstallerExitCode(rc));
      if (!diagnosticsPath.isEmpty())
        currentStep().errorMessage +=
            QStringLiteral("; diagnostics: %1").arg(diagnosticsPath);
      return false;
    } else if (rc != 0) {
      emit logMessage(QStringLiteral("%1 x64 installer returned nonfatal exit code %2")
                          .arg(name)
                          .arg(rc));
    }
    QFile::remove(installerLogPath);
  }

  emit logMessage(QStringLiteral("%1 installed").arg(name));
  return true;
}

bool PrefixSetupRunner::stepDotNetInstall(const QString& url, const QString& name,
                                          const QStringList& knownSha256)
{
  const QString cacheDir = fluorineCacheDir();
  QDir().mkpath(cacheDir);

  const QString filename      = QUrl(url).fileName();
  const QString installerPath = cacheDir + "/" + filename;

  // Download if not cached.
  if (!QFileInfo::exists(installerPath)) {
    emit logMessage(QStringLiteral("Downloading %1...").arg(name));

    if (!downloadRuntimeInstaller(url, installerPath, name, knownSha256)) {
      return false;
    }
  } else if (!downloadRuntimeInstaller(url, installerPath, name, knownSha256)) {
    return false;
  }

  emit logMessage(QStringLiteral("Installing %1...").arg(name));

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  QByteArray installerOutput;
  QString installerLogPath;
  const int rc = runDotNetInstaller(installerPath, name, env,
                                    &installerOutput, &installerLogPath);

  if (!isMicrosoftInstallerSuccess(rc)) {
    const QString diagnosticsPath = reportInstallerFailure(
        name, installerPath, rc, installerOutput, installerLogPath);
    currentStep().errorMessage =
        QStringLiteral("%1 installer failed (%2)")
            .arg(name, describeInstallerExitCode(rc));
    if (!diagnosticsPath.isEmpty())
      currentStep().errorMessage +=
          QStringLiteral("; diagnostics: %1").arg(diagnosticsPath);
    return false;
  } else if (rc != 0) {
    emit logMessage(QStringLiteral("%1 installer returned nonfatal exit code %2")
                        .arg(name)
                        .arg(rc));
  }
  QFile::remove(installerLogPath);

  return true;
}

int PrefixSetupRunner::runDotNetInstaller(
    const QString& installerPath,
    const QString& displayName,
    const QMap<QString, QString>& env,
    QByteArray* captured,
    QString* installerLogPath)
{
  const QString logDir = fluorineTmpDir() + "/installer-logs";
  QDir().mkpath(logDir);

  QString safeName = displayName.toLower();
  safeName.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                   QStringLiteral("-"));
  safeName.remove(QRegularExpression(QStringLiteral("^-|-$")));
  const QString timestamp =
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  const QString logPath =
      QDir(logDir).filePath(QStringLiteral("%1-%2.log").arg(safeName, timestamp));
  QFile::remove(logPath);

  if (installerLogPath)
    *installerLogPath = logPath;

  return runProcess(
      m_wineBin,
      {installerPath, "/install", "/quiet", "/norestart", "/log",
       linuxPathToWineZPath(logPath)},
      env, -1, captured);
}

QStringList PrefixSetupRunner::storageDiagnostics(const QString& installerPath) const
{
  auto formatBytes = [](qint64 bytes) {
    if (bytes < 0)
      return QStringLiteral("unknown");
    return QStringLiteral("%1 bytes (%2 GiB)")
        .arg(bytes)
        .arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0),
             0, 'f', 2);
  };

  auto describeStorage = [&formatBytes](const QString& label,
                                        const QStorageInfo& storage) {
    return QStringLiteral(
               "%1: root=%2 device=%3 filesystem=%4 total=%5 free=%6 "
               "available=%7 readOnly=%8 ready=%9 valid=%10")
        .arg(label,
             storage.rootPath(),
             QString::fromUtf8(storage.device()),
             QString::fromUtf8(storage.fileSystemType()),
             formatBytes(storage.bytesTotal()),
             formatBytes(storage.bytesFree()),
             formatBytes(storage.bytesAvailable()),
             storage.isReadOnly() ? QStringLiteral("yes") : QStringLiteral("no"),
             storage.isReady() ? QStringLiteral("yes") : QStringLiteral("no"),
             storage.isValid() ? QStringLiteral("yes") : QStringLiteral("no"));
  };

  QStringList lines;
  lines << QStringLiteral("Relevant paths:")
        << describeStorage(QStringLiteral("prefix (%1)").arg(m_prefixPath),
                           QStorageInfo(m_prefixPath))
        << describeStorage(
               QStringLiteral("cache (%1)").arg(fluorineCacheDir()),
               QStorageInfo(fluorineCacheDir()))
        << describeStorage(
               QStringLiteral("temporary (%1)").arg(fluorineTmpDir()),
               QStorageInfo(fluorineTmpDir()))
        << describeStorage(
               QStringLiteral("installer (%1)").arg(installerPath),
               QStorageInfo(installerPath));

  const QDir dosDevices(QDir(m_prefixPath).filePath(QStringLiteral("dosdevices")));
  lines << QStringLiteral("Wine drive mappings:");
  const QStringList drives =
      dosDevices.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
  if (drives.isEmpty()) {
    lines << QStringLiteral("(none found)");
  } else {
    for (const QString& drive : drives) {
      const QFileInfo info(dosDevices.filePath(drive));
      lines << QStringLiteral("%1 -> %2")
                   .arg(drive, info.isSymLink() ? info.symLinkTarget()
                                                : info.absoluteFilePath());
    }
  }

  lines << QStringLiteral("Mounted filesystems:");
  QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
  std::sort(volumes.begin(), volumes.end(),
            [](const QStorageInfo& left, const QStorageInfo& right) {
              return left.rootPath() < right.rootPath();
            });
  for (const QStorageInfo& storage : volumes)
    lines << describeStorage(storage.rootPath(), storage);

  return lines;
}

QString PrefixSetupRunner::reportInstallerFailure(
    const QString& displayName,
    const QString& installerPath,
    int exitCode,
    const QByteArray& processOutput,
    const QString& installerLogPath)
{
  const QStringList storageLines = storageDiagnostics(installerPath);
  emit logMessage(QStringLiteral("--- Storage diagnostics ---"));
  for (const QString& line : storageLines)
    emit logMessage(line);

  QByteArray installerLog;
  QFile sourceLog(installerLogPath);
  if (sourceLog.open(QIODevice::ReadOnly))
    installerLog = sourceLog.readAll();

  emit logMessage(QStringLiteral("--- Microsoft installer log (%1) ---")
                      .arg(installerLogPath));
  if (installerLog.isEmpty()) {
    emit logMessage(QStringLiteral("(installer did not produce a log)"));
  } else {
    emit logMessage(QString::fromUtf8(installerLog));
  }

  const QString diagnosticsDir = fluorineDataDir() + "/logs";
  QDir().mkpath(diagnosticsDir);
  const QString timestamp =
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  QString safeName = displayName.toLower();
  safeName.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                   QStringLiteral("-"));
  safeName.remove(QRegularExpression(QStringLiteral("^-|-$")));
  const QString diagnosticsPath = QDir(diagnosticsDir).filePath(
      QStringLiteral("dependency-installer-%1-%2.log").arg(safeName, timestamp));

  QFile diagnostics(diagnosticsPath);
  if (!diagnostics.open(QIODevice::WriteOnly | QIODevice::Text)) {
    emit logMessage(
        QStringLiteral("Failed to write installer diagnostics: %1")
            .arg(diagnosticsPath));
    return {};
  }

  diagnostics.write(QStringLiteral(
                        "Dependency installer: %1\nInstaller: %2\nExit code: %3\n"
                        "Prefix: %4\nProton: %5\nWine: %6\n\n")
                        .arg(displayName, installerPath)
                        .arg(exitCode)
                        .arg(m_prefixPath, m_protonPath, m_wineBin)
                        .toUtf8());
  diagnostics.write("=== Storage diagnostics ===\n");
  diagnostics.write(storageLines.join(QLatin1Char('\n')).toUtf8());
  diagnostics.write("\n\n=== Raw process output ===\n");
  diagnostics.write(processOutput.isEmpty() ? QByteArray("(no console output)\n")
                                             : processOutput);
  diagnostics.write("\n\n=== Microsoft installer log ===\n");
  diagnostics.write(installerLog.isEmpty() ? QByteArray("(no installer log)\n")
                                            : installerLog);
  diagnostics.close();

  emit logMessage(
      QStringLiteral("Full installer diagnostics written to: %1")
          .arg(diagnosticsPath));
  return diagnosticsPath;
}

bool PrefixSetupRunner::stepNuGetSignaturePolicy()
{
  emit logMessage("Configuring NuGet signature policy...");

  const QString usersRoot = QDir(m_prefixPath).filePath("drive_c/users");
  QDir usersDir(usersRoot);
  if (!usersDir.exists()) {
    if (!usersDir.mkpath(QStringLiteral("."))) {
      currentStep().errorMessage = "Failed to create Wine users directory";
      return false;
    }
  }

  QStringList userNames =
      usersDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
  userNames.removeAll(QStringLiteral("Public"));
  userNames.removeAll(QStringLiteral("Default"));
  userNames.removeAll(QStringLiteral("Default User"));

  if (userNames.isEmpty()) {
    userNames << QStringLiteral("steamuser");
  }

  int written = 0;
  for (const QString& userName : userNames) {
    if (isCancelled()) {
      return false;
    }

    const QString nugetDir = usersDir.filePath(
        userName + QStringLiteral("/AppData/Roaming/NuGet"));
    if (!QDir().mkpath(nugetDir)) {
      emit logMessage(QStringLiteral("Skipping NuGet config for %1: cannot create %2")
                          .arg(userName, nugetDir));
      continue;
    }

    const QString configPath = QDir(nugetDir).filePath(QStringLiteral("NuGet.Config"));
    if (QFileInfo::exists(configPath)) {
      emit logMessage(QStringLiteral(
          "NuGet.Config already exists for %1; leaving user config unchanged.")
                          .arg(userName));
      continue;
    }

    QFile config(configPath);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) {
      emit logMessage(QStringLiteral("Skipping NuGet config for %1: cannot write %2")
                          .arg(userName, configPath));
      continue;
    }
    config.write(NUGET_CONFIG_TEMPLATE);
    config.close();
    ++written;
  }

  const QString dotnetRoot =
      QDir(m_prefixPath).filePath("drive_c/Program Files/dotnet");
  const QString dotnetExe = QDir(dotnetRoot).filePath(QStringLiteral("dotnet.exe"));
  if (!QFileInfo::exists(dotnetExe)) {
    currentStep().errorMessage = ".NET SDK install did not provide dotnet.exe";
    return false;
  }

  const QString dotnetSdkRoot = QDir(dotnetRoot).filePath(QStringLiteral("sdk"));
  QDir sdkDir(dotnetSdkRoot);
  const QStringList sdkVersions =
      sdkDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  QString codeRoots;
  QString timestampRoots;
  for (auto it = sdkVersions.crbegin(); it != sdkVersions.crend(); ++it) {
    const QString trustedRoots =
        sdkDir.filePath(*it + QStringLiteral("/trustedroots"));
    const QString candidateCodeRoots =
        QDir(trustedRoots).filePath(QStringLiteral("codesignctl.pem"));
    const QString candidateTimestampRoots =
        QDir(trustedRoots).filePath(QStringLiteral("timestampctl.pem"));
    if (QFileInfo::exists(candidateCodeRoots) &&
        QFileInfo::exists(candidateTimestampRoots)) {
      codeRoots = candidateCodeRoots;
      timestampRoots = candidateTimestampRoots;
      emit logMessage(QStringLiteral("Found .NET trusted root bundles in SDK %1").arg(*it));
      break;
    }
  }

  if (codeRoots.isEmpty() || timestampRoots.isEmpty()) {
    currentStep().errorMessage = ".NET SDK trusted root bundles were not found";
    return false;
  }

  const QString importerDir = QDir(fluorineTmpDir()).filePath(
      QStringLiteral("nuget-cert-importer"));
  QDir(importerDir).removeRecursively();
  if (!QDir().mkpath(importerDir)) {
    currentStep().errorMessage = "Failed to create certificate importer workspace";
    return false;
  }

  QFile projectFile(QDir(importerDir).filePath(QStringLiteral("FluorineCertImport.csproj")));
  if (!projectFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    currentStep().errorMessage = "Failed to write certificate importer project";
    return false;
  }
  projectFile.write(CERT_IMPORTER_CSPROJ);
  projectFile.close();

  QFile programFile(QDir(importerDir).filePath(QStringLiteral("Program.cs")));
  if (!programFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    currentStep().errorMessage = "Failed to write certificate importer program";
    return false;
  }
  programFile.write(CERT_IMPORTER_PROGRAM);
  programFile.close();

  QMap<QString, QString> env = baseWineEnv();
  env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1";
  env["DOTNET_NOLOGO"] = "1";
  env["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1";
  env["NUGET_XMLDOC_MODE"] = "skip";
  env["NUGET_CERT_REVOCATION_MODE"] = "offline";
  env["NUGET_EXPERIMENTAL_CHAIN_BUILD_RETRY_POLICY"] = "10,1000";
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  emit logMessage("Importing .NET code-signing and timestamp roots into Wine...");
  const QString wineImporterDir = linuxPathToWineZPath(importerDir);
  const QString wineCodeRoots = linuxPathToWineZPath(codeRoots);
  const QString wineTimestampRoots = linuxPathToWineZPath(timestampRoots);
  const int rc = runProcess(m_wineBin,
                            {dotnetExe, QStringLiteral("run"),
                             QStringLiteral("--project"), wineImporterDir,
                             QStringLiteral("-p:UseSharedCompilation=false"),
                             QStringLiteral("--"),
                             QStringLiteral("Root"), wineCodeRoots,
                             QStringLiteral("Root"), wineTimestampRoots},
                            env);
  QDir(importerDir).removeRecursively();
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("NuGet certificate import failed (exit code %1)").arg(rc);
    return false;
  }

  emit logMessage(QStringLiteral("NuGet signature policy configured for %1 Wine user(s).")
                      .arg(written));
  return true;
}

bool PrefixSetupRunner::stepGameDetection()
{
  emit logMessage("Auto-detecting installed games...");

  GameScanResult scanResult = detectAllGames();
  if (scanResult.games.isEmpty()) {
    emit logMessage("No games detected");
    return true;
  }

  // Build a single .reg file with all game registry entries.
  QString regContent = QStringLiteral("Windows Registry Editor Version 5.00\n\n");
  int gameCount = 0;

  for (const DetectedGame& game : scanResult.games) {
    if (game.registry_path.isEmpty() || game.registry_value.isEmpty())
      continue;

    const QString& gameName    = game.name;
    const QString& installPath = game.install_path;
    const QString& rPath       = game.registry_path;
    const QString& rVal        = game.registry_value;

    // Convert Linux path to Wine Z: drive path with escaped backslashes.
    // Trailing backslash required — game launchers expect it (matches Steam's format).
    QString winePath = "Z:" + QString(installPath).replace('/', "\\\\");
    if (!winePath.endsWith("\\\\"))
      winePath += "\\\\";

    regContent += QStringLiteral(
        "[HKEY_LOCAL_MACHINE\\%1]\n\"%2\"=\"%3\"\n\n"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%4]\n\"%5\"=\"%6\"\n\n")
        .arg(rPath, rVal, winePath,
             rPath.mid(rPath.indexOf('\\') + 1),
             rVal, winePath);

    emit logMessage(QStringLiteral("  Found: %1 -> %2").arg(gameName, installPath));
    ++gameCount;
  }

  if (gameCount == 0) {
    emit logMessage("No games with valid registry paths");
    return true;
  }

  // Write and import the combined .reg file in a single regedit call.
  const QString tmpDir = fluorineTmpDir();
  QDir().mkpath(tmpDir);

  const QString regFile = tmpDir + "/game_registry.reg";
  QFile f(regFile);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    currentStep().errorMessage = "Failed to write game registry file";
    return false;
  }
  f.write(regContent.toUtf8());
  f.close();

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";
  env["PROTON_USE_XALIA"] = "0";

  emit logMessage(QStringLiteral("Applying registry for %1 game(s)...").arg(gameCount));
  const int rc = runProcess(m_wineBin, {"regedit", regFile}, env);
  QFile::remove(regFile);

  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("wine regedit failed (exit code %1)").arg(rc);
    return false;
  }

  emit logMessage(QStringLiteral("Configured %1 game(s) in registry").arg(gameCount));
  return true;
}

bool PrefixSetupRunner::stepWineRegistry()
{
  emit logMessage("Applying Wine registry settings...");

  const QString tmpDir = fluorineTmpDir();
  QDir().mkpath(tmpDir);

  const QString regFile = tmpDir + "/wine_settings.reg";
  QFile f(regFile);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    currentStep().errorMessage = "Failed to write registry file";
    return false;
  }
  f.write(WINE_SETTINGS_REG);
  f.close();

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";
  env["PROTON_USE_XALIA"] = "0";

  const int rc = runProcess(m_wineBin, {"regedit", regFile}, env);
  QFile::remove(regFile);

  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("wine regedit failed (exit code %1)").arg(rc);
    return false;
  }

  if (!applyDllOverrides(DIRECTX_NATIVE_DLLS, DIRECTX_NATIVE_BUILTIN_DLLS + VCRUN_DLLS))
    return false;

  emit logMessage("Registry settings applied successfully");
  return true;
}

bool PrefixSetupRunner::stepWin11Mode()
{
  emit logMessage("Setting Windows 11 mode...");

  const QString binDir = fluorineBinDir();

  QMap<QString, QString> env;
  env["WINE"]       = m_wineBin;
  env["WINESERVER"] = m_wineserverBin;
  env["WINEPREFIX"] = m_prefixPath;

  const QString shellCmd = QStringLiteral(
      "export PATH='%1':\"$PATH\"; exec '%2' -q win11")
      .arg(binDir, m_winetricksPath);

  const int rc = runProcess("/bin/sh", {"-c", shellCmd}, env);
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("winetricks win11 failed (exit code %1)").arg(rc);
    return false;
  }

  return true;
}

bool PrefixSetupRunner::stepPostSetup()
{
  emit logMessage("Running post-setup tasks...");

  // Ensure AppData temp directory exists.
  ensureTempDirectory(m_prefixPath);

  // Create game symlinks.
  createGameSymlinksAuto(m_prefixPath);

  emit logMessage("Post-setup complete");
  return true;
}

// ============================================================================
// Tool management
// ============================================================================

static void setExecPermissions(const QString& path)
{
  QFile::setPermissions(path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::ExeGroup |
      QFileDevice::ReadOther | QFileDevice::ExeOther);
}

bool PrefixSetupRunner::downloadFile(const QString& url, const QString& destPath,
                                     const QString& displayName)
{
  QSaveFile file(destPath);
  if (!file.open(QIODevice::WriteOnly)) {
    emit logMessage(QStringLiteral("Failed to write: %1").arg(destPath));
    return false;
  }

  // Use Qt networking — no dependency on host curl.
  QNetworkAccessManager nam;
  QNetworkRequest request{QUrl(url)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader, "Fluorine-Manager");
  request.setTransferTimeout(60'000);

  const QString shownName =
      displayName.isEmpty() ? QFileInfo(destPath).fileName() : displayName;
  QElapsedTimer timer;
  timer.start();

  QNetworkReply* reply = nam.get(request);
  QEventLoop loop;
  bool writeFailed = false;
  emit downloadStarted(shownName);
  QObject::connect(reply, &QIODevice::readyRead, this,
                   [&file, reply, &writeFailed] {
                     if (writeFailed)
                       return;
                     const QByteArray data = reply->readAll();
                     if (file.write(data) != data.size()) {
                       writeFailed = true;
                       reply->abort();
                     }
                   });
  QObject::connect(reply, &QNetworkReply::downloadProgress,
                   this,
                   [this, shownName, &timer](qint64 received, qint64 total) {
                     const qint64 elapsedMs = timer.elapsed();
                     const double bytesPerSecond =
                         elapsedMs > 0
                             ? static_cast<double>(received) * 1000.0 /
                                   static_cast<double>(elapsedMs)
                             : 0.0;
                     emit downloadProgress(shownName, received, total,
                                           bytesPerSecond);
                   });
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  QTimer cancelTimer;
  QObject::connect(&cancelTimer, &QTimer::timeout, this, [this, reply] {
    if (isCancelled())
      reply->abort();
  });
  cancelTimer.start(100);
  loop.exec();
  cancelTimer.stop();

  // Drain any final bytes made available together with finished().
  if (!writeFailed) {
    const QByteArray data = reply->readAll();
    if (file.write(data) != data.size())
      writeFailed = true;
  }

  if (isCancelled() || reply->error() != QNetworkReply::NoError || writeFailed) {
    if (isCancelled()) {
      emit logMessage(QStringLiteral("Download cancelled: %1").arg(shownName));
    } else if (writeFailed) {
      emit logMessage(QStringLiteral("Failed while writing: %1").arg(destPath));
    } else {
      emit logMessage(
          QStringLiteral("Download failed with network error %1").arg(reply->error()));
    }
    file.cancelWriting();
    reply->deleteLater();
    emit downloadFinished();
    return false;
  }

  if (!file.commit()) {
    emit logMessage(QStringLiteral("Failed to save: %1").arg(destPath));
    reply->deleteLater();
    emit downloadFinished();
    return false;
  }

  reply->deleteLater();
  emit downloadFinished();
  return true;
}

bool PrefixSetupRunner::downloadRuntimeInstaller(const QString& url,
                                                 const QString& destPath,
                                                 const QString& displayName,
                                                 const QStringList& knownSha256)
{
  const QString stampPath = destPath + ".sha256";
  bool refreshExisting = false;
  bool knownHashMatch = false;
  if (QFileInfo::exists(destPath) &&
      validateRuntimeInstaller(destPath, displayName, knownSha256,
                               &knownHashMatch)) {
    if (knownSha256.isEmpty() || knownHashMatch)
      return true;

    const QString cachedSha = fileSha256(destPath);
    QFile stamp(stampPath);
    if (!cachedSha.isEmpty() && stamp.open(QIODevice::ReadOnly | QIODevice::Text) &&
        QString::fromUtf8(stamp.readAll()).trimmed().compare(
            cachedSha, Qt::CaseInsensitive) == 0) {
      emit logMessage(QStringLiteral(
          "Cached %1 has a previously accepted rolling hash.").arg(displayName));
      return true;
    }

    emit logMessage(QStringLiteral(
        "Cached %1 has an unknown hash, refreshing once...").arg(displayName));
    refreshExisting = true;
  }

  if (QFileInfo::exists(destPath) && !refreshExisting) {
    emit logMessage(QStringLiteral("Cached %1 failed validation, re-downloading...")
                        .arg(displayName));
    QFile::remove(destPath);
  }

  const QString partPath = destPath + ".part";
  QFile::remove(partPath);
  if (!downloadFile(url, partPath, displayName)) {
    QFile::remove(partPath);
    currentStep().errorMessage =
        QStringLiteral("Failed to download %1").arg(displayName);
    return false;
  }

  if (!validateRuntimeInstaller(partPath, displayName, knownSha256,
                                &knownHashMatch)) {
    QFile::remove(partPath);
    return false;
  }

  QFile::remove(destPath);
  if (!QFile::rename(partPath, destPath)) {
    QFile::remove(partPath);
    currentStep().errorMessage =
        QStringLiteral("Failed to cache %1").arg(displayName);
    return false;
  }

  QFile stamp(stampPath);
  if (stamp.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    stamp.write(fileSha256(destPath).toUtf8());
    stamp.write("\n");
  }

  return true;
}

bool PrefixSetupRunner::validateRuntimeInstaller(const QString& path,
                                                 const QString& displayName,
                                                 const QStringList& knownSha256,
                                                 bool* knownHashMatch)
{
  if (knownHashMatch)
    *knownHashMatch = false;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    currentStep().errorMessage =
        QStringLiteral("Failed to read %1").arg(displayName);
    return false;
  }

  if (file.size() < 1024 * 1024) {
    currentStep().errorMessage =
        QStringLiteral("%1 is too small to be a valid installer").arg(displayName);
    return false;
  }

  const QByteArray magic = file.read(2);
  if (magic != "MZ") {
    currentStep().errorMessage =
        QStringLiteral("%1 is not a Windows PE installer").arg(displayName);
    return false;
  }
  file.close();

  const QString sha = fileSha256(path);
  if (sha.isEmpty()) {
    currentStep().errorMessage =
        QStringLiteral("Failed to hash %1").arg(displayName);
    return false;
  }

  emit logMessage(QStringLiteral("%1 SHA256: %2").arg(displayName, sha));

  if (!knownSha256.isEmpty()) {
    const bool matched = knownSha256.contains(sha, Qt::CaseInsensitive);
    if (knownHashMatch)
      *knownHashMatch = matched;
    if (!matched) {
      emit logMessage(
          QStringLiteral(
              "WARNING: %1 hash is not in the known list; accepting because "
              "the installer passed PE validation and Microsoft may have "
              "updated the rolling download.")
              .arg(displayName));
    }
  }

  return true;
}

QString PrefixSetupRunner::fileSha256(const QString& filePath)
{
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return {};

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return {};

  return QString::fromLatin1(hash.result().toHex());
}

bool PrefixSetupRunner::verifySha256(const QString& filePath, const QString& expectedHex)
{
  return fileSha256(filePath).compare(expectedHex, Qt::CaseInsensitive) == 0;
}

bool PrefixSetupRunner::downloadAndVerify(const QString& url, const QString& destPath,
                                          const QString& expectedSha256)
{
  if (!downloadFile(url, destPath)) {
    currentStep().errorMessage = QStringLiteral("Failed to download %1").arg(url);
    return false;
  }

  if (!verifySha256(destPath, expectedSha256)) {
    QFile::remove(destPath);
    currentStep().errorMessage =
        QStringLiteral("SHA256 mismatch for %1").arg(QUrl(url).fileName());
    return false;
  }

  return true;
}

bool PrefixSetupRunner::downloadAndVerifyAny(const QStringList& urls,
                                             const QString& destPath,
                                             const QStringList& expectedSha256)
{
  const QString partPath = destPath + QStringLiteral(".part");
  QFile::remove(partPath);

  for (int i = 0; i < urls.size(); ++i) {
    if (isCancelled()) {
      QFile::remove(partPath);
      currentStep().errorMessage = "Download cancelled";
      return false;
    }

    const QString& url = urls.at(i);
    if (i > 0) {
      emit logMessage(
          QStringLiteral("Trying fallback download %1 of %2...")
              .arg(i)
              .arg(urls.size() - 1));
    }

    if (!downloadFile(url, partPath)) {
      QFile::remove(partPath);
      continue;
    }

    const QString sha = fileSha256(partPath);
    if (!expectedSha256.contains(sha, Qt::CaseInsensitive)) {
      emit logMessage(
          QStringLiteral("Downloaded file from %1 has unexpected SHA256: %2")
              .arg(QUrl(url).host(), sha));
      QFile::remove(partPath);
      continue;
    }

    QFile::remove(destPath);
    if (!QFile::rename(partPath, destPath)) {
      QFile::remove(partPath);
      currentStep().errorMessage =
          QStringLiteral("Failed to cache %1").arg(QFileInfo(destPath).fileName());
      return false;
    }

    emit logMessage(
        QStringLiteral("Verified %1 (SHA256 %2)")
            .arg(QFileInfo(destPath).fileName(), sha));
    return true;
  }

  currentStep().errorMessage = QStringLiteral(
      "Failed to download a verified %1 from %2 source(s)")
                                   .arg(QFileInfo(destPath).fileName())
                                   .arg(urls.size());
  return false;
}

bool PrefixSetupRunner::ensure7zz()
{
  const QString binDir = fluorineBinDir();
  m_7zzPath = binDir + "/7zz";

  if (QFileInfo::exists(m_7zzPath))
    return true;

  emit logMessage("Downloading 7-Zip...");
  QDir().mkpath(binDir);

  const QString tarPath = binDir + "/7zz.tar.xz";

  if (!downloadFile(SEVENZIP_URL, tarPath)) {
    emit logMessage("ERROR: Failed to download 7-Zip");
    return false;
  }

  // Extract 7zz binary from the tar.xz using host tar (always available).
  const int rc = runHostProcess(
      "tar",
      {"xf", tarPath, "-C", binDir, "7zz"});

  QFile::remove(tarPath);

  if (rc != 0 || !QFileInfo::exists(m_7zzPath)) {
    emit logMessage("ERROR: Failed to extract 7-Zip");
    return false;
  }

  setExecPermissions(m_7zzPath);
  return true;
}

bool PrefixSetupRunner::ensureWinetricks()
{
  const QString binDir = fluorineBinDir();
  QDir().mkpath(binDir);
  m_winetricksPath = binDir + "/winetricks";

  emit logMessage("Checking for winetricks...");

  if (!downloadFile(WINETRICKS_URL, m_winetricksPath)) {
    if (!QFileInfo::exists(m_winetricksPath)) {
      emit logMessage("ERROR: Failed to download winetricks");
      return false;
    }
    // Existing copy is fine.
  }

  setExecPermissions(m_winetricksPath);
  return true;
}

bool PrefixSetupRunner::ensureCabextract()
{
  const QString binDir         = fluorineBinDir();
  const QString cabextractPath = binDir + "/cabextract";

  if (QFileInfo::exists(cabextractPath))
    return true;

  emit logMessage("Downloading cabextract...");
  QDir().mkpath(binDir);

  const QString zipPath = binDir + "/cabextract.zip";

  if (!downloadFile(CABEXTRACT_URL, zipPath)) {
    emit logMessage("ERROR: Failed to download cabextract");
    return false;
  }

  // Extract using our downloaded 7zz (no host unzip dependency).
  if (!ensure7zz()) {
    QFile::remove(zipPath);
    return false;
  }

  const int rc = runHostProcess(
      m_7zzPath,
      {"x", zipPath, "-o" + binDir, "-y"});

  QFile::remove(zipPath);

  if (rc != 0 || !QFileInfo::exists(cabextractPath)) {
    emit logMessage("ERROR: Failed to extract cabextract");
    return false;
  }

  setExecPermissions(cabextractPath);
  return true;
}

// ============================================================================
// Wine environment helpers
// ============================================================================

QString PrefixSetupRunner::findWineBinary() const
{
  for (const char* subdir : {"files/bin", "dist/bin"}) {
    const QString candidate = QDir(m_protonPath).filePath(
        QString::fromLatin1(subdir) + "/wine");
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return {};
}

QString PrefixSetupRunner::findWineserverBinary() const
{
  for (const char* subdir : {"files/bin", "dist/bin"}) {
    const QString candidate = QDir(m_protonPath).filePath(
        QString::fromLatin1(subdir) + "/wineserver");
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return {};
}

QString PrefixSetupRunner::findProtonScript() const
{
  const QString script = QDir(m_protonPath).filePath("proton");
  return QFileInfo::exists(script) ? script : QString();
}

QString PrefixSetupRunner::detectSteamPath()
{
  // Use native Steam detection first.
  const QString steamPath = findSteamPath();
  if (!steamPath.isEmpty())
    return steamPath;

  // Fallback.
  const QString home = QDir::homePath();
  const QStringList candidates = {
      home + "/.local/share/Steam",
      home + "/.steam/steam",
      home + "/.steam/root",
  };
  for (const QString& p : candidates) {
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString PrefixSetupRunner::detectSLRRunScript() const
{
  // Check Fluorine-downloaded SLR first (steamrt4 preferred, sniper fallback).
  const QStringList nakCandidates = {
      fluorineDataDir() + "/steamrt/SteamLinuxRuntime_4/run",
      fluorineDataDir() + "/steamrt/SteamLinuxRuntime_sniper/run",
  };
  for (const QString& p : nakCandidates) {
    const QFileInfo fi(p);
    if (fi.exists() && fi.isExecutable())
      return p;
  }

  const QString steamPath = detectSteamPath();

  const QStringList candidates = {
      steamPath + "/steamapps/common/SteamLinuxRuntime_4/run",
      steamPath + "/steamapps/common/SteamLinuxRuntime_sniper/run",
      QDir::homePath() + "/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4/run",
      QDir::homePath() + "/.local/share/Steam/steamapps/common/SteamLinuxRuntime_sniper/run",
      "/usr/lib/pressure-vessel/wrap",
  };

  for (const QString& p : candidates) {
    const QFileInfo fi(p);
    if (!p.isEmpty() && fi.exists() && fi.isExecutable())
      return p;
  }
  return {};
}

bool PrefixSetupRunner::ensureSLRRunScript()
{
  if (!m_slrRunScript.isEmpty()) {
    emit logMessage(
        QStringLiteral("Using Steam Linux Runtime: %1").arg(m_slrRunScript));
    return true;
  }

  emit logMessage(
      "Steam Linux Runtime is required for Proton prefix initialization; "
      "installing steamrt4...");
  emit downloadStarted(QStringLiteral("Steam Linux Runtime"));

  const SlrCancellationSource cancellation([this] { return isCancelled(); });

  const QString err = downloadSlr(
      nullptr,
      [this](const QString& msg) { emit logMessage(msg); },
      cancellation.token());

  emit downloadFinished();

  if (cancellation.isCancellationRequested() ||
      slrOperationAdmissionSuppressed()) {
    currentStep().errorMessage = "Steam Linux Runtime download cancelled";
    return false;
  }

  if (!err.isEmpty()) {
    currentStep().errorMessage =
        QStringLiteral("Steam Linux Runtime install failed: %1").arg(err);
    return false;
  }

  m_slrRunScript = detectSLRRunScript();
  if (m_slrRunScript.isEmpty()) {
    currentStep().errorMessage =
        "Steam Linux Runtime installed but run script was not found";
    return false;
  }

  emit logMessage(
      QStringLiteral("Using Steam Linux Runtime: %1").arg(m_slrRunScript));
  return true;
}

QString PrefixSetupRunner::fluorineBinDir()
{
  return fluorineDataDir() + "/bin";
}

void PrefixSetupRunner::killStalePrefixProcesses() const
{
  if (m_prefixPath.isEmpty())
    return;

  const QString cleanPrefix = QDir::cleanPath(m_prefixPath);
  const QString cleanCompat = QDir::cleanPath(QDir(m_prefixPath).filePath(".."));

  QDir procDir("/proc");
  const QStringList pids =
      procDir.entryList({QStringLiteral("[0-9]*")}, QDir::Dirs);

  QList<qint64> victims;
  for (const QString& pid : pids) {
    // Read cmdline (fast filter for wine-like processes).
    QFile cmdF("/proc/" + pid + "/cmdline");
    if (!cmdF.open(QIODevice::ReadOnly))
      continue;
    QByteArray cmdline = cmdF.readAll();
    if (cmdline.isEmpty())
      continue;
    const QString cmdStr = QString::fromUtf8(cmdline.replace('\0', ' '));
    const bool wineLike = cmdStr.contains("wineboot") ||
                          cmdStr.contains("wineserver") ||
                          cmdStr.contains("pv-adverb") ||
                          cmdStr.contains("wine-preloader") ||
                          cmdStr.contains("steam.exe");
    if (!wineLike)
      continue;

    // Definitive match: process's own WINEPREFIX env points at our prefix.
    // Wine processes have Windows-style cmdlines ("c:\windows\...") that
    // don't include the Linux prefix path, so cmdline-matching misses them.
    bool mine = cmdStr.contains(cleanPrefix) || cmdStr.contains(cleanCompat);
    if (!mine) {
      QFile envF("/proc/" + pid + "/environ");
      if (envF.open(QIODevice::ReadOnly)) {
        QByteArray environ = envF.readAll();
        for (const QByteArray& kv : environ.split('\0')) {
          if (kv.startsWith("WINEPREFIX=")) {
            const QString val = QString::fromUtf8(kv.mid(11));
            if (QDir::cleanPath(val) == cleanPrefix)
              mine = true;
            break;
          }
          if (kv.startsWith("STEAM_COMPAT_DATA_PATH=")) {
            const QString val = QString::fromUtf8(kv.mid(23));
            if (QDir::cleanPath(val) == cleanCompat)
              mine = true;
          }
        }
      }
    }

    if (mine) {
      bool ok = false;
      const qint64 p = pid.toLongLong(&ok);
      if (ok)
        victims.append(p);
    }
  }

  if (victims.isEmpty())
    return;

  MOBase::log::warn("Found {} stale wine process(es) bound to prefix — killing",
                    victims.size());
  for (qint64 p : victims)
    ::kill(static_cast<pid_t>(p), SIGTERM);

  QThread::msleep(300);

  for (qint64 p : victims) {
    if (::kill(static_cast<pid_t>(p), 0) == 0)
      ::kill(static_cast<pid_t>(p), SIGKILL);
  }

  QThread::msleep(100);
}

QString PrefixSetupRunner::fluorineCacheDir()
{
  return fluorineDataDir() + "/cache";
}

QString PrefixSetupRunner::fluorineTmpDir()
{
  return fluorineDataDir() + "/tmp";
}

QMap<QString, QString> PrefixSetupRunner::baseWineEnv() const
{
  QMap<QString, QString> env;
  env["WINEPREFIX"] = m_prefixPath;
  env["WINE"]       = m_wineBin;
  env["WINESERVER"] = m_wineserverBin;
  return env;
}

bool PrefixSetupRunner::applyDllOverrides(const QStringList& native,
                                          const QStringList& nativeBuiltin)
{
  if (native.isEmpty() && nativeBuiltin.isEmpty())
    return true;

  const QString tmpDir = fluorineTmpDir();
  QDir().mkpath(tmpDir);

  QString regContent = QStringLiteral(
      "Windows Registry Editor Version 5.00\n\n"
      "[HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides]\n");

  auto appendOverrides = [&regContent](const QStringList& dlls,
                                       const QString& mode) {
    for (const QString& dll : dlls) {
      regContent += QStringLiteral("\"*%1\"=\"%2\"\n").arg(dll, mode);
    }
  };

  appendOverrides(native, QStringLiteral("native"));
  appendOverrides(nativeBuiltin, QStringLiteral("native,builtin"));
  regContent += QLatin1Char('\n');

  const QString regFile = tmpDir + "/dependency_overrides.reg";
  QFile f(regFile);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    currentStep().errorMessage = "Failed to write dependency override registry";
    return false;
  }
  f.write(regContent.toUtf8());
  f.close();

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";
  env["PROTON_USE_XALIA"] = "0";

  const int rc = runProcess(m_wineBin, {"regedit", regFile}, env);
  QFile::remove(regFile);
  if (rc != 0) {
    currentStep().errorMessage =
        QStringLiteral("dependency override import failed (exit code %1)").arg(rc);
    return false;
  }

  return true;
}

QString PrefixSetupRunner::makeDllOverrideEnv(const QString& base,
                                              const QStringList& native,
                                              const QStringList& nativeBuiltin)
{
  QStringList entries;
  if (!base.trimmed().isEmpty())
    entries.append(base.split(';', Qt::SkipEmptyParts));

  for (const QString& dll : native)
    entries.append(QStringLiteral("%1=n").arg(dll));
  for (const QString& dll : nativeBuiltin)
    entries.append(QStringLiteral("%1=n,b").arg(dll));

  return entries.join(QLatin1Char(';'));
}

bool PrefixSetupRunner::isMicrosoftInstallerSuccess(int exitCode)
{
  // Wine exposes only the low byte of Windows process exit codes:
  // 1641 (restart initiated) -> 105, 3010 (restart required) -> 194, and
  // 1638 (another/newer version is installed) -> 102.
  return exitCode == 0 || exitCode == 102 || exitCode == 105 || exitCode == 194;
}

QString PrefixSetupRunner::describeInstallerExitCode(int exitCode)
{
  switch (exitCode) {
  case 5:
    return QStringLiteral("exit code 5: installer was cancelled or access was denied");
  case 112:
    return QStringLiteral(
        "exit code 112: not enough disk space in the prefix or target filesystem");
  case 105:
    return QStringLiteral("exit code 105: installer requested restart now");
  case 194:
    return QStringLiteral("exit code 194: installer requested restart later");
  case 102:
    return QStringLiteral("exit code 102: newer version is already installed");
  default:
    return QStringLiteral("exit code %1").arg(exitCode);
  }
}

SetupStep& PrefixSetupRunner::currentStep()
{
  if (m_currentStepIndex >= 0 && m_currentStepIndex < m_steps.size())
    return m_steps[m_currentStepIndex];
  return m_steps.last();
}

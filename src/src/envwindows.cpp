#include "envwindows.h"
#include "env.h"
#include "envmodule.h"
#include <log.h>
#include <utility.h>

#ifndef _WIN32
#include <sys/utsname.h>
#include <unistd.h>
#include <fstream>
#include <string>
#endif

namespace env
{

using namespace MOBase;

#ifdef _WIN32

WindowsInfo::WindowsInfo()
{
  // loading ntdll.dll, the functions will be found with GetProcAddress()
  LibraryPtr ntdll(LoadLibraryW(L"ntdll.dll"));

  if (!ntdll) {
    log::error("failed to load ntdll.dll while getting version");
    return;
  } else {
    m_reported = getReportedVersion(ntdll.get());
    m_real     = getRealVersion(ntdll.get());
  }

  m_release  = getRelease();
  m_elevated = getElevated();
}

WindowsInfo::Version WindowsInfo::getReportedVersion(HINSTANCE ntdll) const
{
  using RtlGetVersionType = NTSTATUS(NTAPI)(PRTL_OSVERSIONINFOW);

  auto* RtlGetVersion =
      reinterpret_cast<RtlGetVersionType*>(GetProcAddress(ntdll, "RtlGetVersion"));

  if (!RtlGetVersion) {
    log::error("RtlGetVersion() not found in ntdll.dll");
    return {};
  }

  OSVERSIONINFOEX vi     = {};
  vi.dwOSVersionInfoSize = sizeof(vi);

  RtlGetVersion((RTL_OSVERSIONINFOW*)&vi);

  return {vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber};
}

WindowsInfo::Version WindowsInfo::getRealVersion(HINSTANCE ntdll) const
{
  using RtlGetNtVersionNumbersType = void(NTAPI)(DWORD*, DWORD*, DWORD*);

  auto* RtlGetNtVersionNumbers = reinterpret_cast<RtlGetNtVersionNumbersType*>(
      GetProcAddress(ntdll, "RtlGetNtVersionNumbers"));

  if (!RtlGetNtVersionNumbers) {
    log::error("RtlGetNtVersionNumbers not found in ntdll.dll");
    return {};
  }

  DWORD major = 0, minor = 0, build = 0;
  RtlGetNtVersionNumbers(&major, &minor, &build);

  // for whatever reason, the build number has 0xf0000000 set
  build = 0x0fffffff & build;

  return {major, minor, build};
}

WindowsInfo::Release WindowsInfo::getRelease() const
{
  QSettings settings(
      R"(HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
      QSettings::NativeFormat);

  Release r;

  r.buildLab = settings.value("BuildLabEx", "").toString();
  if (r.buildLab.isEmpty()) {
    r.buildLab = settings.value("BuildLab", "").toString();
    if (r.buildLab.isEmpty()) {
      r.buildLab = settings.value("BuildBranch", "").toString();
    }
  }

  r.ID = settings.value("DisplayVersion", "").toString();
  if (r.ID.isEmpty()) {
    r.ID = settings.value("ReleaseId", "").toString();
  }

  r.UBR = settings.value("UBR", 0).toUInt();

  return r;
}

std::optional<bool> WindowsInfo::getElevated() const
{
  HandlePtr token;

  {
    HANDLE rawToken = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
      const auto e = GetLastError();

      log::error("while trying to check if process is elevated, "
                 "OpenProcessToken() failed: {}",
                 formatSystemMessage(e));

      return {};
    }

    token.reset(rawToken);
  }

  TOKEN_ELEVATION e = {};
  DWORD size        = sizeof(TOKEN_ELEVATION);

  if (!GetTokenInformation(token.get(), TokenElevation, &e, sizeof(e), &size)) {
    const auto e = GetLastError();

    log::error("while trying to check if process is elevated, "
               "GetTokenInformation() failed: {}",
               formatSystemMessage(e));

    return {};
  }

  return (e.TokenIsElevated != 0);
}

#else  // Linux

WindowsInfo::WindowsInfo()
{
  m_reported = getKernelVersion();
  m_real     = m_reported;
  m_release  = getRelease();
  m_elevated = getElevated();
}

WindowsInfo::Version WindowsInfo::getKernelVersion() const
{
  struct utsname uts;
  if (uname(&uts) != 0) {
    log::error("uname() failed");
    return {};
  }

  Version v;

  // Parse kernel version string like "6.18.9-2-cachyos"
  QString kver = QString::fromUtf8(uts.release);
  QStringList parts = kver.split('.');

  if (parts.size() >= 1) v.major = parts[0].toUInt();
  if (parts.size() >= 2) v.minor = parts[1].toUInt();
  if (parts.size() >= 3) {
    // Build may contain suffix like "9-2-cachyos", take numeric prefix
    QString buildStr = parts[2];
    int dashPos = buildStr.indexOf('-');
    if (dashPos >= 0) {
      buildStr = buildStr.left(dashPos);
    }
    v.build = buildStr.toUInt();
  }

  return v;
}

WindowsInfo::Release WindowsInfo::getRelease() const
{
  Release r;

  // Parse /etc/os-release
  std::ifstream osRelease("/etc/os-release");
  if (osRelease.is_open()) {
    std::string line;
    while (std::getline(osRelease, line)) {
      // Lines are KEY=VALUE or KEY="VALUE"
      auto eqPos = line.find('=');
      if (eqPos == std::string::npos) continue;

      std::string key = line.substr(0, eqPos);
      std::string val = line.substr(eqPos + 1);

      // Strip quotes
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val = val.substr(1, val.size() - 2);
      }

      if (key == "PRETTY_NAME") {
        r.buildLab = QString::fromStdString(val);
      } else if (key == "VERSION_ID") {
        r.ID = QString::fromStdString(val);
      }
    }
  }

  return r;
}

std::optional<bool> WindowsInfo::getElevated() const
{
  return (geteuid() == 0);
}

#endif  // _WIN32

bool WindowsInfo::compatibilityMode() const
{
#ifdef _WIN32
  if (m_real == Version()) {
    // don't know the real version, can't guess compatibility mode
    return false;
  }

  return (m_real != m_reported);
#else
  // compatibility mode doesn't apply on Linux
  return false;
#endif
}

const WindowsInfo::Version& WindowsInfo::reportedVersion() const
{
  return m_reported;
}

const WindowsInfo::Version& WindowsInfo::realVersion() const
{
  return m_real;
}

const WindowsInfo::Release& WindowsInfo::release() const
{
  return m_release;
}

std::optional<bool> WindowsInfo::isElevated() const
{
  return m_elevated;
}

QString WindowsInfo::toString() const
{
  QStringList sl;

  const QString reported = m_reported.toString();
  const QString real     = m_real.toString();

  // version
  sl.push_back("version " + reported);

  // real version if different
  if (compatibilityMode()) {
    sl.push_back("real version " + real);
  }

#ifdef _WIN32
  // build.UBR, such as 17763.557
  if (m_release.UBR != 0) {
    DWORD build = 0;

    if (compatibilityMode()) {
      build = m_real.build;
    } else {
      build = m_reported.build;
    }

    sl.push_back(QString("%1.%2").arg(build).arg(m_release.UBR));
  }
#endif

  // release ID
  if (!m_release.ID.isEmpty()) {
    sl.push_back("release " + m_release.ID);
  }

  // buildlab string / distro name
  if (!m_release.buildLab.isEmpty()) {
    sl.push_back(m_release.buildLab);
  }

  // elevated
  QString elevated = "?";
  if (m_elevated.has_value()) {
    elevated = (*m_elevated ? "yes" : "no");
  }

  sl.push_back("elevated: " + elevated);

  return sl.join(", ");
}

}  // namespace env

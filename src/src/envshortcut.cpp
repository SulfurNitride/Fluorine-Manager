#include "envshortcut.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <utility>

#include "executableslist.h"
#include "desktopshortcutpolicy.h"
#include "instancemanager.h"

namespace env
{

// Prefer the fluorine-manager launcher script over the bare ModOrganizer-core
// binary — the launcher sets up bundled library paths, Qt plugin paths, etc.
// Without it, shortcuts fail on systems that don't have all deps in PATH.
QString launcherOrBinary()
{
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString launcher = appDir + "/fluorine-manager";
  if (QFile::exists(launcher)) {
    return QFileInfo(launcher).absoluteFilePath();
  }
  return QFileInfo(qApp->applicationFilePath()).absoluteFilePath();
}

Shortcut::Shortcut() : m_iconIndex(0) {}

static QString shortcutOwnerId(const QString& instanceName, const QString& name,
                               const QString& arguments)
{
  return desktopshortcut::ownerId(instanceName + QLatin1Char('\n') + name +
                                  QLatin1Char('\n') + arguments);
}

static bool shortcutFileHasOwner(const QString& path, const QString& owner,
                                 bool script)
{
  const QFileInfo info(path);
  if (!info.exists() || info.isSymLink() || !info.isFile()) {
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray contents = file.read(64 * 1024);
  return script ? desktopshortcut::scriptFileHasOwner(contents, owner)
                : desktopshortcut::desktopFileHasOwner(contents, owner);
}

static bool shortcutPathCanBeWritten(const QString& path, const QString& owner,
                                     bool script)
{
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) {
    return true;
  }
  return shortcutFileHasOwner(path, owner, script);
}

// ---------------------------------------------------------------------------
// PE icon extractor — reads the largest icon embedded in a Windows .exe
// and returns it as a QImage.  Returns a null QImage on failure.
// ---------------------------------------------------------------------------
static QImage extractIconFromExe(const QString& exePath)
{
  QFile f(exePath);
  if (!f.open(QIODevice::ReadOnly))
    return {};

  auto peek = [&](qint64 offset, qint64 size) -> QByteArray {
    if (offset < 0 || offset + size > f.size())
      return {};
    f.seek(offset);
    return f.read(size);
  };

  auto u16 = [](const char* p) -> quint16 {
    return static_cast<quint16>(static_cast<unsigned char>(p[0])) |
           (static_cast<quint16>(static_cast<unsigned char>(p[1])) << 8);
  };
  auto u32 = [](const char* p) -> quint32 {
    return static_cast<quint32>(static_cast<unsigned char>(p[0])) |
           (static_cast<quint32>(static_cast<unsigned char>(p[1])) << 8) |
           (static_cast<quint32>(static_cast<unsigned char>(p[2])) << 16) |
           (static_cast<quint32>(static_cast<unsigned char>(p[3])) << 24);
  };

  // DOS header: check MZ magic, read PE offset at 0x3C.
  QByteArray dosHdr = peek(0, 64);
  if (dosHdr.size() < 64 || dosHdr[0] != 'M' || dosHdr[1] != 'Z')
    return {};

  quint32 const peOff = u32(dosHdr.constData() + 0x3C);

  // PE signature.
  QByteArray peSig = peek(peOff, 4);
  if (peSig.size() < 4 || peSig[0] != 'P' || peSig[1] != 'E')
    return {};

  // COFF header (20 bytes) immediately after the 4-byte PE signature.
  QByteArray const coffHdr = peek(peOff + 4, 20);
  if (coffHdr.size() < 20)
    return {};
  quint16 const numSections     = u16(coffHdr.constData() + 2);
  quint16 const optionalHdrSize = u16(coffHdr.constData() + 16);

  // Optional header — we need the magic (PE32 vs PE32+) and the
  // resource data directory entry (index 2).
  qint64 const optOff = peOff + 4 + 20;
  QByteArray const optHdr = peek(optOff, optionalHdrSize);
  if (optHdr.size() < 4)
    return {};

  quint16 const magic = u16(optHdr.constData());
  int ddOffset; // offset of DataDirectory[0] inside optional header
  if (magic == 0x10b)       // PE32
    ddOffset = 96;
  else if (magic == 0x20b)  // PE32+
    ddOffset = 112;
  else
    return {};

  // DataDirectory[2] = resource table (each entry is 8 bytes: RVA + Size).
  int const rsrcDDOff = ddOffset + 2 * 8;
  if (rsrcDDOff + 8 > optHdr.size())
    return {};
  quint32 rsrcRVA  = u32(optHdr.constData() + rsrcDDOff);
  if (rsrcRVA == 0)
    return {};

  // Section headers — find the section containing rsrcRVA.
  qint64 const secOff = optOff + optionalHdrSize;
  quint32 rsrcFileOff = 0;
  for (int i = 0; std::cmp_less(i , numSections); ++i) {
    QByteArray const sec = peek(secOff + i * 40, 40);
    if (sec.size() < 40)
      return {};
    quint32 const virtAddr = u32(sec.constData() + 12);
    quint32 const virtSize = u32(sec.constData() + 8);
    quint32 const rawOff   = u32(sec.constData() + 20);
    if (rsrcRVA >= virtAddr && rsrcRVA < virtAddr + virtSize) {
      rsrcFileOff = rawOff + (rsrcRVA - virtAddr);
      break;
    }
  }
  if (rsrcFileOff == 0)
    return {};

  // Helper: convert an RVA inside the .rsrc section to a file offset.
  auto rvaToFile = [&](quint32 rva) -> qint64 {
    return static_cast<qint64>(rsrcFileOff) + (rva - rsrcRVA);
  };

  // Parse a resource directory and return (id, offset, isDir) entries.
  struct DirEntry { quint32 id; quint32 offset; bool isDir; };
  auto readDir = [&](quint32 dirFileOff) -> std::vector<DirEntry> {
    std::vector<DirEntry> entries;
    QByteArray const dh = peek(dirFileOff, 16);
    if (dh.size() < 16) return entries;
    quint16 const numNamed = u16(dh.constData() + 12);
    quint16 const numId    = u16(dh.constData() + 14);
    int const total = numNamed + numId;
    QByteArray const ea = peek(dirFileOff + 16, total * 8);
    if (ea.size() < total * 8) return entries;
    for (int i = 0; i < total; ++i) {
      quint32 const nameOrId = u32(ea.constData() + i * 8);
      quint32 off      = u32(ea.constData() + i * 8 + 4);
      bool const isDir       = (off & 0x80000000u) != 0;
      off &= 0x7FFFFFFFu;
      entries.push_back({nameOrId, off, isDir});
    }
    return entries;
  };

  // Level 0: find RT_GROUP_ICON (14) and RT_ICON (3).
  auto level0 = readDir(rsrcFileOff);
  quint32 groupIconOff = 0;
  quint32 iconOff = 0;
  for (auto& e : level0) {
    if (e.id == 14 && e.isDir) groupIconOff = rsrcFileOff + e.offset;
    if (e.id == 3 && e.isDir)  iconOff = rsrcFileOff + e.offset;
  }
  if (groupIconOff == 0 || iconOff == 0)
    return {};

  // Level 1 of RT_GROUP_ICON: pick the first group.
  auto groupL1 = readDir(groupIconOff);
  if (groupL1.empty() || !groupL1[0].isDir)
    return {};

  // Level 2: pick the first language.
  auto groupL2 = readDir(rsrcFileOff + groupL1[0].offset);
  if (groupL2.empty() || groupL2[0].isDir)
    return {};

  // Read the data entry (RVA + size).
  QByteArray const dataEntry = peek(rsrcFileOff + groupL2[0].offset, 16);
  if (dataEntry.size() < 16)
    return {};
  quint32 const grpDataRVA  = u32(dataEntry.constData());
  quint32 const grpDataSize = u32(dataEntry.constData() + 4);
  QByteArray const grpData  = peek(rvaToFile(grpDataRVA), grpDataSize);
  if (grpData.size() < 6)
    return {};

  // Parse the GRPICONDIR: pick the icon entry with the largest area.
  quint16 const iconCount = u16(grpData.constData() + 4);
  if (grpData.size() < 6 + iconCount * 14)
    return {};

  int bestIdx    = -1;
  int bestArea   = 0;
  quint16 bestId = 0;
  for (int i = 0; std::cmp_less(i , iconCount); ++i) {
    const char* e = grpData.constData() + 6 + i * 14;
    int w = static_cast<unsigned char>(e[0]);
    int h = static_cast<unsigned char>(e[1]);
    if (w == 0) w = 256;
    if (h == 0) h = 256;
    int const area = w * h;
    if (area > bestArea) {
      bestArea = area;
      bestIdx  = i;
      bestId   = u16(e + 12);
    }
  }
  if (bestIdx < 0)
    return {};

  // Find RT_ICON with matching ID.
  auto iconL1 = readDir(iconOff);
  quint32 iconEntryOff = 0;
  for (auto& e : iconL1) {
    if (e.id == bestId && e.isDir) {
      auto iconL2 = readDir(rsrcFileOff + e.offset);
      if (!iconL2.empty() && !iconL2[0].isDir)
        iconEntryOff = rsrcFileOff + iconL2[0].offset;
      break;
    }
  }
  if (iconEntryOff == 0)
    return {};

  QByteArray const iconDE = peek(iconEntryOff, 16);
  if (iconDE.size() < 16)
    return {};
  quint32 const iconDataRVA  = u32(iconDE.constData());
  quint32 const iconDataSize = u32(iconDE.constData() + 4);
  QByteArray const iconData  = peek(rvaToFile(iconDataRVA), iconDataSize);
  if (iconData.isEmpty())
    return {};

  // Try loading as PNG first (256x256 icons are typically stored as PNG).
  QImage img;
  img.loadFromData(iconData, "PNG");
  if (!img.isNull())
    return img;

  // Otherwise it's a raw BITMAPINFOHEADER (DIB). Wrap it in a minimal
  // .ico so Qt's ICO reader can handle it.
  QByteArray ico;
  QBuffer buf(&ico);
  buf.open(QIODevice::WriteOnly);
  // ICONDIR header
  const char icoHdr[6] = {0, 0, 1, 0, 1, 0}; // reserved=0, type=1, count=1
  buf.write(icoHdr, 6);
  // ICONDIRENTRY (use the values from the group icon entry)
  const char* grpE = grpData.constData() + 6 + bestIdx * 14;
  char entry[16];
  memcpy(entry, grpE, 12);            // copy w,h,colorCount,reserved,planes,bitCount,size
  quint32 const dataOff = 6 + 16;           // offset to icon data = after header + 1 entry
  entry[12] = dataOff & 0xFF;
  entry[13] = (dataOff >> 8) & 0xFF;
  entry[14] = (dataOff >> 16) & 0xFF;
  entry[15] = (dataOff >> 24) & 0xFF;
  // Fix the size field (bytes 8-11) to match actual data size
  entry[8]  = iconDataSize & 0xFF;
  entry[9]  = (iconDataSize >> 8) & 0xFF;
  entry[10] = (iconDataSize >> 16) & 0xFF;
  entry[11] = (iconDataSize >> 24) & 0xFF;
  buf.write(entry, 16);
  buf.write(iconData);
  buf.close();

  img.loadFromData(ico, "ICO");
  return img;
}

// Return the path to the installed hicolor copy of the Fluorine icon.
// Empty string if not present.
static QString bundledFluorineIcon()
{
  QString hicolor =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      "/icons/hicolor/256x256/apps/com.fluorine.manager.png";
  if (QFile::exists(hicolor))
    return hicolor;
  return {};
}

// Check whether an icon file is just the Fluorine fallback icon
// (same file size as the bundled one).
static bool isFallbackIcon(const QString& iconPath)
{
  QString const bundled = bundledFluorineIcon();
  if (bundled.isEmpty())
    return false;
  return QFileInfo(iconPath).size() == QFileInfo(bundled).size();
}

// Install a game-specific icon to $XDG_DATA_HOME/icons/fluorine/ and return
// the absolute path.  Tries to extract the icon from exePath (.exe) first,
// then falls back to the bundled Fluorine icon.
//
// If a cached icon exists but is just the fallback, re-attempts extraction
// in case the executable has changed or was previously unavailable.
static QString installIcon(const QString& iconBaseName, const QString& exePath = {})
{
  QString const iconDir =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      "/icons/fluorine";
  QString iconDest = iconDir + "/" + iconBaseName + ".png";

  // If the icon already exists and is NOT the fallback, keep it.
  if (QFile::exists(iconDest) && !isFallbackIcon(iconDest)) {
    return iconDest;
  }

  QDir().mkpath(iconDir);

  // Try to extract the icon from the .exe file.
  if (!exePath.isEmpty()) {
    QImage icon = extractIconFromExe(exePath);
    if (!icon.isNull()) {
      if (icon.width() != 256 || icon.height() != 256) {
        icon = icon.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      }
      // Remove stale fallback before saving the real icon.
      QFile::remove(iconDest);
      if (icon.save(iconDest, "PNG")) {
        return iconDest;
      }
    }
  }

  // Already have a fallback cached — no need to copy again.
  if (QFile::exists(iconDest)) {
    return iconDest;
  }

  // Install the bundled Fluorine icon as a fallback.
  QString const bundled = bundledFluorineIcon();
  if (!bundled.isEmpty()) {
    QFile::copy(bundled, iconDest);
    return iconDest;
  }

  return "com.fluorine.manager";
}

Shortcut::Shortcut(const Executable& exe) : Shortcut()
{
  const auto i = *InstanceManager::singleton().currentInstance();

  m_name         = exe.title();
  m_instanceName = i.displayName();
  m_target       = launcherOrBinary();

  // For portable instances, use the absolute directory path so MO2 can
  // find it (line 595 in instancemanager.cpp handles abs path lookup).
  // For global instances, use the display name.
  QString const instanceId = i.isPortable() ? QDir(i.directory()).absolutePath()
                                      : i.displayName();
  m_arguments = desktopshortcut::commandLineToken(
      QString("moshortcut://%1:%2").arg(instanceId, exe.title()));

  m_description = QString("Run %1 with Fluorine").arg(exe.title());

  // Try to extract the icon from the executable (works for .exe files).
  // For native Linux binaries with a custom icon, use that directly.
  // Falls back to the bundled Fluorine icon.
  QString const exePath = exe.binaryInfo().absoluteFilePath();
  if (exe.usesOwnIcon() && !exePath.endsWith(".exe", Qt::CaseInsensitive)) {
    m_icon = exePath;
  }
  if (m_icon.isEmpty()) {
    QString const iconBase = desktopshortcut::filenameStem(
        m_instanceName, m_name,
        shortcutOwnerId(m_instanceName, m_name, m_arguments));
    // Keep the Fluorine fallback separate so newly-created shortcuts do not
    // reuse cached program artwork when the icon preference is disabled.
    m_icon = exe.usesOwnIcon() ? installIcon(iconBase, exePath)
                              : installIcon(iconBase + "-fluorine");
  }

  m_workingDirectory = QFileInfo(m_target).absolutePath();
}

Shortcut& Shortcut::name(const QString& s)
{
  m_name = s;
  return *this;
}

Shortcut& Shortcut::target(const QString& s)
{
  m_target = s;
  return *this;
}

Shortcut& Shortcut::arguments(const QString& s)
{
  m_arguments = s;
  return *this;
}

Shortcut& Shortcut::description(const QString& s)
{
  m_description = s;
  return *this;
}

Shortcut& Shortcut::icon(const QString& s, int index)
{
  m_icon      = s;
  m_iconIndex = index;
  return *this;
}

Shortcut& Shortcut::workingDirectory(const QString& s)
{
  m_workingDirectory = s;
  return *this;
}

bool Shortcut::exists(Locations loc) const
{
  return shortcutFileHasOwner(
      shortcutPath(loc), shortcutOwnerId(m_instanceName, m_name, m_arguments),
      false);
}

bool Shortcut::toggle(Locations loc)
{
  if (exists(loc)) {
    return remove(loc);
  } else {
    return add(loc);
  }
}

bool Shortcut::add(Locations loc)
{
  if (loc != Desktop && loc != ApplicationMenu) {
    return false;
  }

  const QString path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  QDir().mkpath(QFileInfo(path).absolutePath());

  const QString script = scriptPath(loc);
  if (script.isEmpty()) {
    return false;
  }

  const QString owner = shortcutOwnerId(m_instanceName, m_name, m_arguments);
  if (!shortcutPathCanBeWritten(path, owner, false) ||
      !shortcutPathCanBeWritten(script, owner, true)) {
    return false;
  }

  const QFileInfo scriptInfo(script);
  const bool scriptAlreadyExisted = scriptInfo.exists() || scriptInfo.isSymLink();

  QSaveFile scriptFile(script);
  if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }

  QTextStream scriptOut(&scriptFile);
  scriptOut << "#!/usr/bin/env bash\n";
  scriptOut << "# fluorine-shortcut-owner=" << owner << "\n";
  scriptOut << "set -euo pipefail\n";
  if (!m_workingDirectory.isEmpty()) {
    scriptOut << "cd " << desktopshortcut::shellQuote(m_workingDirectory) << "\n";
  }
  scriptOut << "exec " << desktopshortcut::shellCommand(m_target, m_arguments)
            << "\n";
  scriptOut.flush();
  if (!scriptFile.commit()) {
    return false;
  }
  if (!QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                         QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                         QFileDevice::ExeOther)) {
    if (!scriptAlreadyExisted && shortcutFileHasOwner(script, owner, true)) {
      QFile::remove(script);
    }
    return false;
  }

  const QString desktopExec = desktopshortcut::desktopExecCommandLine(
      {QStringLiteral("/usr/bin/env"), QStringLiteral("bash"),
       QStringLiteral("--"), script});
  if (desktopExec.isEmpty()) {
    if (!scriptAlreadyExisted && shortcutFileHasOwner(script, owner, true)) {
      QFile::remove(script);
    }
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (!scriptAlreadyExisted && shortcutFileHasOwner(script, owner, true)) {
      QFile::remove(script);
    }
    return false;
  }

  // Include instance name in the display name to distinguish shortcuts
  // for the same executable across different instances (e.g. "NVSE (New Vegas)"
  // vs "NVSE (New Vegas 2)").
  QString displayName = m_name.isEmpty() ? "Fluorine" : m_name;
  if (!m_instanceName.isEmpty()) {
    displayName += " (" + m_instanceName + ")";
  }

  QTextStream out(&file);
  out << "[Desktop Entry]\n";
  out << "Type=Application\n";
  out << "Name=" << desktopshortcut::desktopEntryValue(displayName) << "\n";
  out << "X-Fluorine-Shortcut-Id=" << owner << "\n";
  if (!m_description.isEmpty()) {
    out << "Comment=" << desktopshortcut::desktopEntryValue(m_description)
        << "\n";
  }
  out << "Exec=" << desktopExec << "\n";
  if (!m_workingDirectory.isEmpty()) {
    out << "Path=" << desktopshortcut::desktopEntryValue(m_workingDirectory)
        << "\n";
  }
  if (!m_icon.isEmpty()) {
    out << "Icon=" << desktopshortcut::desktopEntryValue(m_icon) << "\n";
  }
  out << "Terminal=false\n";
  out << "Categories=Game;\n";

  out.flush();
  if (!file.commit()) {
    if (!scriptAlreadyExisted && shortcutFileHasOwner(script, owner, true)) {
      QFile::remove(script);
    }
    return false;
  }

  // Make it executable (required by some desktop environments)
  if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther)) {
    return false;
  }

  return true;
}

bool Shortcut::remove(Locations loc)
{
  const QString path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }
  const QString owner = shortcutOwnerId(m_instanceName, m_name, m_arguments);
  if (!shortcutFileHasOwner(path, owner, false)) {
    return false;
  }
  const bool removedShortcut = QFile::remove(path);
  const QString script = scriptPath(loc);
  if (!script.isEmpty() && shortcutFileHasOwner(script, owner, true)) {
    QFile::remove(script);
  }
  return removedShortcut;
}

QString Shortcut::shortcutPath(Locations loc) const
{
  const QString dir = shortcutDirectory(loc);
  if (dir.isEmpty()) {
    return {};
  }
  return dir + "/" + shortcutFilename();
}

QString Shortcut::scriptPath(Locations loc) const
{
  const QString dir = shortcutDirectory(loc);
  if (dir.isEmpty()) {
    return {};
  }
  return dir + "/" + scriptFilename();
}

QString Shortcut::shortcutDirectory(Locations loc)
{
  if (loc == Desktop) {
    // XDG desktop directory
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
      return desktop;
    }
    return QDir::homePath() + "/Desktop";
  }
  if (loc == ApplicationMenu) {
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
  }
  return {};
}

QString Shortcut::shortcutFilename() const
{
  return desktopshortcut::filenameStem(
             m_instanceName, m_name,
             shortcutOwnerId(m_instanceName, m_name, m_arguments)) +
         ".desktop";
}

QString Shortcut::scriptFilename() const
{
  return desktopshortcut::filenameStem(
             m_instanceName, m_name,
             shortcutOwnerId(m_instanceName, m_name, m_arguments)) +
         ".sh";
}

QString toString(Shortcut::Locations loc)
{
  switch (loc) {
  case Shortcut::None:
    return "none";

  case Shortcut::Desktop:
    return "desktop";

  case Shortcut::StartMenu:
    return "start menu";

  case Shortcut::ApplicationMenu:
    return "application menu";

  default:
    return QString("? (%1)").arg(static_cast<int>(loc));
  }
}

}  // namespace env

#include "audioruntime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
const QString kGuardName = QStringLiteral("x3daudio1_7.dll");
const QString kMicrosoftName = QStringLiteral("x3daudio1_7_ms.dll");

QString sha256(const QString& path)
{
  QFile file(path);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
  return QString::fromLatin1(hash.result().toHex());
}

QByteArray contents(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Wine is case-insensitive: DXSETUP writes "X3DAudio1_7.dll".
QString findFile(const QDir& dir, const QString& name)
{
  for (const QString& entry :
       dir.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
    if (entry.compare(name, Qt::CaseInsensitive) == 0) return dir.filePath(entry);
  }
  return {};
}

bool removeAll(const QDir& dir, const QString& name)
{
  for (const QString& entry :
       dir.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
    if (entry.compare(name, Qt::CaseInsensitive) == 0 && !QFile::remove(dir.filePath(entry)))
      return false;
  }
  // Dangling symlinks are not listed as files.
  const QFileInfo link(dir.filePath(name));
  return !link.isSymLink() || QFile::remove(link.filePath());
}

// Microsoft's DLL: a regular file exporting both X3DAudio functions, not a
// Wine builtin or placeholder.
bool isMicrosoftX3DAudio(const QString& path, const QString& guardHash)
{
  if (path.isEmpty() || QFileInfo(path).isSymLink()) return false;
  const QByteArray data = contents(path);
  return data.startsWith("MZ") && !data.left(0x200).contains("Wine builtin DLL") &&
         !data.left(0x200).contains("Wine placeholder DLL") &&
         data.contains("X3DAudioCalculate") && data.contains("X3DAudioInitialize") &&
         sha256(path) != guardHash;
}
}  // namespace

bool installAudioGuard(const QString& prefix, const QString& bundleDir, bool& changed,
                       QString& skipped, QString& error)
{
  changed = false;
  skipped.clear();
  error.clear();

  const QString guard = QDir(bundleDir).filePath(kGuardName);
  const QString guardHash = sha256(guard);
  const QByteArray recorded = contents(guard + ".sha256").left(64);
  if (guardHash.isEmpty() || guardHash.toLatin1() != recorded) {
    error = QStringLiteral("The bundled audio guard is missing or damaged at %1; "
                           "reinstall Fluorine").arg(guard);
    return false;
  }

  // Never follow a link into another prefix or a shared runner.
  for (const QString& path : {QStringLiteral("drive_c"), QStringLiteral("drive_c/windows"),
                             QStringLiteral("drive_c/windows/system32")}) {
    const QFileInfo info(prefix + "/" + path);
    if (info.isSymLink() || !info.isDir()) {
      error = QStringLiteral("Unsafe or missing prefix directory: %1").arg(info.filePath());
      return false;
    }
  }
  const QDir system32(prefix + "/drive_c/windows/system32");
  const QString current = findFile(system32, kGuardName);
  const QString microsoft = findFile(system32, kMicrosoftName);
  const bool haveMicrosoft = isMicrosoftX3DAudio(microsoft, guardHash);

  if (!current.isEmpty() && !QFileInfo(current).isSymLink() && sha256(current) == guardHash) {
    if (haveMicrosoft) return true;  // already installed
    // The guard alone cannot load: games would fail to start. Let Wine's
    // builtin X3DAudio load instead.
    if (!removeAll(system32, kGuardName)) {
      error = QStringLiteral("Could not remove an orphaned audio guard: %1").arg(current);
      return false;
    }
    changed = true;
    skipped = QStringLiteral("Microsoft's X3DAudio1_7.dll is missing, so the audio guard was "
                             "removed; recreate the prefix to restore both");
    return true;
  }

  if (isMicrosoftX3DAudio(current, guardHash)) {
    // DXSETUP's copy becomes the forwarding target (replacing an older one).
    if (!removeAll(system32, kMicrosoftName) ||
        !QFile::rename(current, system32.filePath(kMicrosoftName))) {
      error = QStringLiteral("Could not move Microsoft's X3DAudio1_7.dll aside in %1")
                  .arg(system32.path());
      return false;
    }
    changed = true;
  } else if (!haveMicrosoft) {
    skipped = current.isEmpty()
                  ? QStringLiteral("X3DAudio1_7.dll is not installed in the prefix")
                  : QStringLiteral("the prefix uses Wine's builtin X3DAudio1_7.dll "
                                   "(Microsoft's DirectX audio runtime is not installed)");
    return true;
  }

  // Write beside the target, then swap it in.
  const QString staging = system32.filePath(kGuardName + ".fluorine-new");
  QFile::remove(staging);
  const QByteArray data = contents(guard);
  QSaveFile output(staging);
  if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() ||
      !output.commit() || !removeAll(system32, kGuardName) ||
      !QFile::rename(staging, system32.filePath(kGuardName))) {
    QFile::remove(staging);
    error = QStringLiteral("Could not install the audio guard into %1").arg(system32.path());
    return false;
  }
  changed = true;
  return true;
}

bool prefixUsesBundledFAudio(const QString& prefix)
{
  return QFileInfo::exists(prefix + "/.fluorine-faudio/version.txt");
}

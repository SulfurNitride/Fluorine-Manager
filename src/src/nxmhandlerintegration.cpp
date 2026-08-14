#include "nxmhandlerintegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>

#include <algorithm>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace nxm_handler_integration
{
namespace
{
constexpr qsizetype MaxManagedFileSize = 1024 * 1024;
constexpr int LockTimeoutMs             = 1500;

const QString CurrentId = QString::fromLatin1(CurrentDesktopFile);
const QString LegacyId  = QString::fromLatin1(LegacyDesktopFile);
const QString MainId    = QStringLiteral("com.fluorine.manager.desktop");
const QStringList Schemes = {
    QStringLiteral("x-scheme-handler/nxm"),
    QStringLiteral("x-scheme-handler/modl"),
};

enum class LeafKind
{
  Missing,
  Regular,
  Foreign,
  Error,
};

struct Leaf
{
  LeafKind kind{LeafKind::Missing};
  QByteArray bytes;
  QFileDevice::Permissions permissions{};
  QString error;
};

struct Artifact
{
  bool missing{true};
  bool owned{false};
  bool supportsModl{false};
  bool requiresWrapper{false};
  QString error;
};

Result failure(Status status, const QString& path, const QString& message)
{
  return {status, false, path, message};
}

bool pathExists(const QFileInfo& info)
{
  return info.exists() || info.isSymLink();
}

QString leafIdentity(const QString& path)
{
  if (path.isEmpty()) {
    return {};
  }
  struct stat status;
  const QByteArray encoded = QFile::encodeName(path);
  if (::stat(encoded.constData(), &status) == 0) {
    return QStringLiteral("inode:%1:%2")
        .arg(static_cast<qulonglong>(status.st_dev))
        .arg(static_cast<qulonglong>(status.st_ino));
  }

  QString absolute = QFileInfo(path).absoluteFilePath();
  QStringList suffix;
  QFileInfo cursor(absolute);
  while (!pathExists(cursor)) {
    suffix.prepend(cursor.fileName());
    const QString parent = cursor.absolutePath();
    if (parent == cursor.absoluteFilePath()) {
      break;
    }
    cursor.setFile(parent);
  }
  QString base = cursor.canonicalFilePath();
  if (base.isEmpty()) {
    base = cursor.absoluteFilePath();
  }
  for (const QString& part : suffix) {
    base = QDir(base).filePath(part);
  }
  return QStringLiteral("path:") + QDir::cleanPath(base);
}

Paths normalizedPaths(const Paths& input)
{
  Paths paths = input;
  const QString currentDesktopIdentity = leafIdentity(paths.desktop);
  if (!paths.historicalDesktop.isEmpty() &&
      leafIdentity(paths.historicalDesktop) == currentDesktopIdentity) {
    paths.historicalDesktop.clear();
  }

  const QString currentMimeIdentity = leafIdentity(paths.mimeApps);
  QStringList legacyMime;
  QStringList identities;
  for (const QString& candidate : std::as_const(paths.legacyMimeApps)) {
    const QString identity = leafIdentity(candidate);
    if (candidate.isEmpty() || identity == currentMimeIdentity ||
        identities.contains(identity)) {
      continue;
    }
    identities.append(identity);
    legacyMime.append(candidate);
  }
  paths.legacyMimeApps = legacyMime;
  return paths;
}

Leaf readRegularLeaf(const QString& path, bool allowMissing)
{
  if (path.isEmpty()) {
    return allowMissing
               ? Leaf{}
               : Leaf{LeafKind::Error, {}, {}, QStringLiteral("path is empty")};
  }
  const QFileInfo info(path);
  if (!pathExists(info)) {
    if (allowMissing) {
      return {};
    }
    return {LeafKind::Error, {}, {}, QStringLiteral("file does not exist")};
  }
  if (info.isSymLink() || !info.isFile() || info.ownerId() != ::geteuid()) {
    return {LeafKind::Foreign, {}, {},
            QStringLiteral("path is not an owner-controlled regular file")};
  }
  if (info.size() > MaxManagedFileSize) {
    return {LeafKind::Foreign, {}, {},
            QStringLiteral("file is too large to be a managed integration file")};
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {LeafKind::Error, {}, {}, file.errorString()};
  }
  const QByteArray bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    return {LeafKind::Error, {}, {}, file.errorString()};
  }
  return {LeafKind::Regular, bytes, info.permissions(), {}};
}

std::optional<QString> decodeUtf8(const Leaf& leaf)
{
  QStringDecoder decoder(QStringDecoder::Utf8);
  const QString text = decoder.decode(leaf.bytes);
  if (decoder.hasError()) {
    return std::nullopt;
  }
  return text;
}

bool ensureParentDirectory(const QString& path, QString& error)
{
  const QString parent = QFileInfo(path).absolutePath();
  const QFileInfo parentInfo(parent);
  if (pathExists(parentInfo)) {
    const QString canonical = parentInfo.canonicalFilePath();
    const QFileInfo effective(parentInfo.isSymLink() ? canonical : parent);
    if (canonical.isEmpty() || !effective.isDir() ||
        effective.ownerId() != ::geteuid()) {
      error = QStringLiteral("parent is not an owner-controlled directory");
      return false;
    }
    return true;
  }
  if (!QDir().mkpath(parent)) {
    error = QStringLiteral("cannot create parent directory");
    return false;
  }
  const QFileInfo created(parent);
  if (created.isSymLink() || !created.isDir() ||
      created.ownerId() != ::geteuid()) {
    error = QStringLiteral("created parent directory failed validation");
    return false;
  }
  return true;
}

bool writeAtomically(const QString& path, const QByteArray& bytes,
                     QFileDevice::Permissions permissions, QString& error)
{
  if (!ensureParentDirectory(path, error)) {
    return false;
  }
  const QFileInfo existing(path);
  if (pathExists(existing) &&
      (existing.isSymLink() || !existing.isFile() ||
       existing.ownerId() != ::geteuid())) {
    error = QStringLiteral("refusing to replace a non-regular or foreign path");
    return false;
  }

  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    error = file.errorString();
    return false;
  }
  if (!file.setPermissions(permissions) || file.write(bytes) != bytes.size() ||
      !file.flush()) {
    error = file.errorString().isEmpty() ? QStringLiteral("short write")
                                         : file.errorString();
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    error = file.errorString();
    return false;
  }
  return true;
}

QString escapeExecArgument(const QString& value, QString& error)
{
  if (value.contains(QChar::Null) || value.contains(QLatin1Char('\r')) ||
      value.contains(QLatin1Char('\n'))) {
    error = QStringLiteral("launcher path contains a control character");
    return {};
  }

  QString escaped = value;
  escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
  escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
  escaped.replace(QStringLiteral("`"), QStringLiteral("\\`"));
  escaped.replace(QStringLiteral("$"), QStringLiteral("\\$"));
  escaped.replace(QStringLiteral("%"), QStringLiteral("%%"));
  return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

bool structurallyManagedDesktop(const QString& text)
{
  QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  if (!lines.isEmpty() && lines.constLast().isEmpty()) {
    lines.removeLast();
  }
  if (lines.size() != 7 || lines.at(0) != QStringLiteral("[Desktop Entry]") ||
      lines.at(1) != QStringLiteral("Type=Application") ||
      lines.at(2) != QStringLiteral("Name=Fluorine Manager NXM Handler") ||
      lines.at(4) != QStringLiteral(
                         "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;") ||
      lines.at(5) != QStringLiteral("NoDisplay=true") ||
      lines.at(6) != QStringLiteral("X-Fluorine-Managed=nxm-handler-v1")) {
    return false;
  }
  const QRegularExpression exec(
      QStringLiteral("^Exec=\\\"(?:\\\\.|[^\\\"\\r\\n])*\\\" nxm-handle %u$"));
  return exec.match(lines.at(3)).hasMatch();
}

enum class HistoricalDesktopKind
{
  None,
  Wrapper,
  Flatpak,
};

HistoricalDesktopKind historicalDesktop(const QString& text, bool legacyName,
                                        const QString& wrapperPath)
{
  QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  if (!lines.isEmpty() && lines.constLast().isEmpty()) {
    lines.removeLast();
  }
  if (lines.size() != 6 || lines.at(0) != QStringLiteral("[Desktop Entry]") ||
      lines.at(1) != QStringLiteral("Type=Application") ||
      lines.at(5) != QStringLiteral("NoDisplay=true")) {
    return HistoricalDesktopKind::None;
  }
  const QString expectedName =
      legacyName ? QStringLiteral("Name=Mod Organizer 2 NXM Handler")
                 : QStringLiteral("Name=Fluorine Manager NXM Handler");
  if (lines.at(2) != expectedName ||
      (lines.at(4) != QStringLiteral("MimeType=x-scheme-handler/nxm;") &&
       lines.at(4) != QStringLiteral(
                          "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;"))) {
    return HistoricalDesktopKind::None;
  }
  if (!lines.at(3).startsWith(QStringLiteral("Exec="))) {
    return HistoricalDesktopKind::None;
  }
  const QString exec = lines.at(3).mid(5);
  if (exec == QStringLiteral("flatpak run com.fluorine.manager nxm-handle %u")) {
    return HistoricalDesktopKind::Flatpak;
  }
  if (!wrapperPath.isEmpty() &&
      (exec == wrapperPath + QStringLiteral(" %u") ||
       exec == wrapperPath + QStringLiteral(" nxm-handle %u"))) {
    return HistoricalDesktopKind::Wrapper;
  }
  if (legacyName &&
      exec == QStringLiteral("mo2-nxm-handler nxm-handle %u")) {
    return HistoricalDesktopKind::Wrapper;
  }
  return HistoricalDesktopKind::None;
}

bool historicalWrapper(const QString& text,
                       QFileDevice::Permissions permissions)
{
  if (!text.startsWith(QStringLiteral("#!/bin/sh\n")) ||
      text.contains(QChar::Null) || text.size() > 16 * 1024 ||
      !permissions.testFlag(QFileDevice::ExeOwner)) {
    return false;
  }
  const QRegularExpression oldForm(
      QStringLiteral("\\A#!/bin/sh\\nexec \\\"([^\\r\\n\\\"]+)\\\" nxm-handle \\\"\\$@\\\"\\n\\z"));
  const auto oldMatch = oldForm.match(text);
  if (oldMatch.hasMatch()) {
    return true;
  }
  const QRegularExpression handoffForm(QStringLiteral(
      "\\A#!/bin/sh\\nurl=\\$1\\n\\[ -n \\\"\\$url\\\" \\] \\|\\| exit 2\\n"
      "if \\\"(?<exe>[^\\r\\n\\\"]+)\\\" nxm-handle \\\"\\$url\\\"; then\\n"
      "  exit 0\\nfi\\nexec \\\"\\k<exe>\\\" \\\"\\$url\\\"\\n\\z"));
  const auto handoffMatch = handoffForm.match(text);
  return handoffMatch.hasMatch();
}

Artifact inspectDesktop(const QString& path, bool legacyName,
                        const QString& wrapperPath)
{
  const Leaf leaf = readRegularLeaf(path, true);
  if (leaf.kind == LeafKind::Missing) {
    return {};
  }
  if (leaf.kind != LeafKind::Regular) {
    return {false, false, false, false, leaf.error};
  }
  const auto text = decodeUtf8(leaf);
  if (!text) {
    return {false, false, false, false,
            QStringLiteral("desktop entry is not valid UTF-8")};
  }
  const auto historical = historicalDesktop(*text, legacyName, wrapperPath);
  const bool owned = structurallyManagedDesktop(*text) ||
                     historical != HistoricalDesktopKind::None;
  const bool supportsModl = text->contains(QStringLiteral(
      "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;\n"));
  return {false, owned, supportsModl,
          historical == HistoricalDesktopKind::Wrapper,
          owned ? QString() : QStringLiteral("desktop entry is not Fluorine-owned")};
}

Artifact inspectWrapper(const QString& path)
{
  const Leaf leaf = readRegularLeaf(path, true);
  if (leaf.kind == LeafKind::Missing) {
    return {};
  }
  if (leaf.kind != LeafKind::Regular) {
    return {false, false, false, false, leaf.error};
  }
  const auto text = decodeUtf8(leaf);
  const bool owned = text && historicalWrapper(*text, leaf.permissions);
  return {false, owned, false, false,
          owned ? QString() : QStringLiteral("wrapper is not Fluorine-owned")};
}

QStringList valuesFromEntry(const QString& line)
{
  const int equals = line.indexOf(QLatin1Char('='));
  if (equals < 0) {
    return {};
  }
  QStringList result;
  for (const QString& raw :
       line.mid(equals + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
    const QString value = raw.trimmed();
    if (!value.isEmpty() && !result.contains(value)) {
      result.append(value);
    }
  }
  return result;
}

QString entry(const QString& key, const QStringList& values)
{
  return key + QLatin1Char('=') + values.join(QLatin1Char(';')) +
         QLatin1Char(';');
}

int sectionEnd(const QStringList& lines, int start)
{
  for (int i = start + 1; i < lines.size(); ++i) {
    const QString trimmed = lines.at(i).trimmed();
    if (trimmed.startsWith(QLatin1Char('[')) &&
        trimmed.endsWith(QLatin1Char(']'))) {
      return i;
    }
  }
  return lines.size();
}

bool findUniqueSection(const QStringList& lines, const QString& name, int& start,
                       QString& error)
{
  start = -1;
  for (int i = 0; i < lines.size(); ++i) {
    if (lines.at(i).trimmed() != name) {
      continue;
    }
    if (start >= 0) {
      error = QStringLiteral("duplicate MIME section %1").arg(name);
      return false;
    }
    start = i;
  }
  return true;
}

enum class MimeOperation
{
  Install,
  Cleanup,
  Uninstall,
};

bool updateSection(QStringList& lines, const QString& section,
                   const QString& scheme, MimeOperation operation,
                   bool forceDefault, bool legacyOwned, bool create,
                   QString& error)
{
  int start = -1;
  if (!findUniqueSection(lines, section, start, error)) {
    return false;
  }
  if (start < 0) {
    if (!create) {
      return true;
    }
    if (!lines.isEmpty() && !lines.constLast().isEmpty()) {
      lines.append(QString());
    }
    start = lines.size();
    lines.append(section);
  }

  int found = -1;
  const int end = sectionEnd(lines, start);
  for (int i = start + 1; i < end; ++i) {
    if (!lines.at(i).trimmed().startsWith(scheme + QLatin1Char('='))) {
      continue;
    }
    if (found >= 0) {
      error = QStringLiteral("duplicate MIME entry for %1").arg(scheme);
      return false;
    }
    found = i;
  }

  QStringList values = found >= 0 ? valuesFromEntry(lines.at(found)) : QStringList{};
  const bool currentWasFirst = !values.isEmpty() &&
                               (values.constFirst() == CurrentId ||
                                values.constFirst() == MainId ||
                                (legacyOwned && values.constFirst() == LegacyId));
  const bool removedSection =
      section == QStringLiteral("[Removed Associations]");
  if (operation == MimeOperation::Uninstall && removedSection) {
    // Publish the opt-out before deleting handler artifacts. If the process
    // stops between those commits, desktop discovery must not reactivate a
    // surviving current or strictly proven legacy handler. Preserve an
    // existing blacklist for the historical main desktop and all foreign IDs.
    values.removeAll(CurrentId);
    values.prepend(CurrentId);
    if (legacyOwned) {
      values.removeAll(LegacyId);
      values.insert(1, LegacyId);
    }
  } else {
    values.removeAll(CurrentId);
    if (legacyOwned) {
      values.removeAll(LegacyId);
    }
    // The historical main desktop is not the desired URL handler. Preserve an
    // existing blacklist for it, while removing it from active associations.
    if (!removedSection) {
      values.removeAll(MainId);
    }
  }

  if (operation == MimeOperation::Install) {
    if (section == QStringLiteral("[Default Applications]")) {
      if (forceDefault || currentWasFirst || values.isEmpty()) {
        values.prepend(CurrentId);
      } else {
        values.append(CurrentId);
      }
    } else if (section == QStringLiteral("[Added Associations]")) {
      values.prepend(CurrentId);
    }
  }

  if (found >= 0) {
    if (values.isEmpty()) {
      lines.removeAt(found);
    } else {
      lines[found] = entry(scheme, values);
    }
  } else if (!values.isEmpty()) {
    lines.insert(start + 1, entry(scheme, values));
  }
  return true;
}

bool transformMime(const QString& input, MimeOperation operation,
                   bool forceDefault, bool legacyOwned, QString& output,
                   QString& error)
{
  QStringList lines = input.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  while (!lines.isEmpty() && lines.constLast().isEmpty()) {
    lines.removeLast();
  }
  const QStringList sections = {
      QStringLiteral("[Default Applications]"),
      QStringLiteral("[Added Associations]"),
      QStringLiteral("[Removed Associations]"),
  };
  for (const QString& scheme : Schemes) {
    for (const QString& section : sections) {
      const bool removedSection =
          section == QStringLiteral("[Removed Associations]");
      const bool create = operation == MimeOperation::Install
                              ? !removedSection
                          : operation == MimeOperation::Uninstall
                              ? removedSection
                              : false;
      if (!updateSection(lines, section, scheme, operation, forceDefault,
                         legacyOwned, create, error)) {
        return false;
      }
    }
  }
  output = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
  return true;
}

bool containsManagedMimeEntry(const QString& text, bool legacyOwned)
{
  QString section;
  const QStringList relevantSections = {
      QStringLiteral("[Default Applications]"),
      QStringLiteral("[Added Associations]"),
      QStringLiteral("[Removed Associations]"),
  };
  QStringList ids{CurrentId, MainId};
  if (legacyOwned) {
    ids.append(LegacyId);
  }
  for (const QString& raw : text.split(QLatin1Char('\n'))) {
    const QString line = raw.trimmed();
    if (line.startsWith(QLatin1Char('[')) &&
        line.endsWith(QLatin1Char(']'))) {
      section = line;
      continue;
    }
    if (!relevantSections.contains(section)) {
      continue;
    }
    for (const QString& scheme : Schemes) {
      if (!line.startsWith(scheme + QLatin1Char('='))) {
        continue;
      }
      const QStringList values = valuesFromEntry(line);
      for (const QString& id : ids) {
        if (values.contains(id)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool mimeHasActiveSchemes(const QString& text, const QString& id,
                          const QStringList& requiredSchemes)
{
  QStringList active;
  QStringList removed;
  QString section;
  for (const QString& raw : text.split(QLatin1Char('\n'))) {
    const QString line = raw.trimmed();
    if (line.startsWith(QLatin1Char('[')) &&
        line.endsWith(QLatin1Char(']'))) {
      section = line;
      continue;
    }
    for (const QString& scheme : requiredSchemes) {
      if (!line.startsWith(scheme + QLatin1Char('=')) ||
          !valuesFromEntry(line).contains(id)) {
        continue;
      }
      if (section == QStringLiteral("[Removed Associations]")) {
        removed.append(scheme);
      } else if (section == QStringLiteral("[Default Applications]") ||
                 section == QStringLiteral("[Added Associations]")) {
        active.append(scheme);
      }
    }
  }
  for (const QString& scheme : requiredSchemes) {
    if (!active.contains(scheme) || removed.contains(scheme)) {
      return false;
    }
  }
  return true;
}

Result inspectArtifacts(const Paths& paths, Artifact& current, Artifact& legacy,
                        Artifact& historicalCurrent, Artifact& wrapper)
{
  current = inspectDesktop(paths.desktop, false, paths.legacyWrapper);
  historicalCurrent = inspectDesktop(paths.historicalDesktop, false,
                                     paths.legacyWrapper);
  legacy = inspectDesktop(paths.legacyDesktop, true, paths.legacyWrapper);
  wrapper           = inspectWrapper(paths.legacyWrapper);
  auto requireOwnedWrapper = [&wrapper](Artifact& desktop) {
    if (desktop.owned && desktop.requiresWrapper && !wrapper.owned) {
      desktop.owned = false;
      desktop.error = QStringLiteral(
          "historical desktop is not paired with a Fluorine-owned wrapper");
    }
  };
  requireOwnedWrapper(current);
  requireOwnedWrapper(historicalCurrent);
  requireOwnedWrapper(legacy);
  const bool wrapperPaired =
      wrapper.owned &&
      ((current.owned && current.requiresWrapper) ||
       (historicalCurrent.owned && historicalCurrent.requiresWrapper) ||
       (legacy.owned && legacy.requiresWrapper));
  if (!wrapperPaired) {
    wrapper.owned = false;
  }
  // Only the current publication target is an install/uninstall collision.
  // Generic historical names may belong to another manager and are preserved.
  if (!current.missing && !current.owned) {
    return failure(Status::Collision, paths.desktop, current.error);
  }
  return {Status::Success};
}

Result updateMimeFile(const QString& path, MimeOperation operation,
                      bool forceDefault, bool legacyOwned, bool create)
{
  const Leaf leaf = readRegularLeaf(path, true);
  if (leaf.kind == LeafKind::Missing && !create) {
    return {Status::NoChange};
  }
  if (leaf.kind == LeafKind::Foreign) {
    return failure(Status::Collision, path, leaf.error);
  }
  if (leaf.kind == LeafKind::Error) {
    return failure(Status::IoError, path, leaf.error);
  }
  const auto decoded = leaf.kind == LeafKind::Missing
                           ? std::optional<QString>(QString())
                           : decodeUtf8(leaf);
  if (!decoded) {
    return failure(Status::Collision, path,
                   QStringLiteral("mimeapps.list is not valid UTF-8"));
  }
  if (operation != MimeOperation::Install && !create &&
      !containsManagedMimeEntry(*decoded, legacyOwned)) {
    return {Status::NoChange};
  }

  QString transformed;
  QString error;
  if (!transformMime(*decoded, operation, forceDefault, legacyOwned,
                     transformed, error)) {
    return failure(Status::Collision, path, error);
  }
  const QByteArray output = transformed.toUtf8();
  if (leaf.kind == LeafKind::Regular && output == leaf.bytes) {
    return {Status::NoChange};
  }
  const auto permissions = leaf.kind == LeafKind::Regular
                               ? leaf.permissions
                               : QFileDevice::ReadOwner |
                                     QFileDevice::WriteOwner |
                                     QFileDevice::ReadGroup |
                                     QFileDevice::ReadOther;
  if (!writeAtomically(path, output, permissions, error)) {
    return failure(Status::IoError, path, error);
  }
  return {Status::Success, true};
}

Result preflightMimeFile(const QString& path, MimeOperation operation,
                         bool forceDefault, bool legacyOwned, bool create)
{
  const Leaf leaf = readRegularLeaf(path, true);
  if (leaf.kind == LeafKind::Missing && !create) {
    return {Status::NoChange};
  }
  if (leaf.kind == LeafKind::Foreign) {
    return failure(Status::Collision, path, leaf.error);
  }
  if (leaf.kind == LeafKind::Error) {
    return failure(Status::IoError, path, leaf.error);
  }
  const auto decoded = leaf.kind == LeafKind::Missing
                           ? std::optional<QString>(QString())
                           : decodeUtf8(leaf);
  if (!decoded) {
    return failure(Status::Collision, path,
                   QStringLiteral("mimeapps.list is not valid UTF-8"));
  }
  if (operation != MimeOperation::Install && !create &&
      !containsManagedMimeEntry(*decoded, legacyOwned)) {
    return {Status::NoChange};
  }
  QString transformed;
  QString error;
  if (!transformMime(*decoded, operation, forceDefault, legacyOwned,
                     transformed, error)) {
    return failure(Status::Collision, path, error);
  }
  return {Status::Success};
}

enum class ArtifactType
{
  CurrentDesktop,
  LegacyDesktop,
  LegacyWrapper,
};

bool removeOwned(const QString& path, const Artifact& artifact,
                 ArtifactType type, const QString& wrapperPath, QString& error)
{
  if (artifact.missing) {
    return true;
  }
  if (!artifact.owned) {
    return true;
  }
  // Revalidate immediately before unlink. A nested event loop or another
  // desktop tool may have replaced the path since preflight; ownership of the
  // former inode does not authorize deleting the replacement.
  const Leaf current = readRegularLeaf(path, true);
  if (current.kind == LeafKind::Missing) {
    return true;
  }
  const auto text = current.kind == LeafKind::Regular
                        ? decodeUtf8(current)
                        : std::optional<QString>{};
  const bool stillOwned = text &&
                          (type == ArtifactType::CurrentDesktop
                               ? structurallyManagedDesktop(*text) ||
                                     historicalDesktop(*text, false, wrapperPath) !=
                                         HistoricalDesktopKind::None
                           : type == ArtifactType::LegacyDesktop
                               ? historicalDesktop(*text, true, wrapperPath) !=
                                     HistoricalDesktopKind::None
                               : historicalWrapper(*text,
                                                   current.permissions));
  if (!stillOwned) {
    error = QStringLiteral("integration file changed after preflight");
    return false;
  }
  if (!QFile::remove(path)) {
    error = QStringLiteral("failed to remove owned integration file");
    return false;
  }
  return true;
}

}  // namespace

QString desktopEntry(const QString& launcher, QString* error)
{
  QString localError;
  const QString escaped = escapeExecArgument(launcher, localError);
  if (escaped.isEmpty()) {
    if (error) {
      *error = localError;
    }
    return {};
  }
  return QStringLiteral(
             "[Desktop Entry]\n"
             "Type=Application\n"
             "Name=Fluorine Manager NXM Handler\n"
             "Exec=%1 nxm-handle %u\n"
             "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;\n"
             "NoDisplay=true\n"
             "X-Fluorine-Managed=nxm-handler-v1\n")
      .arg(escaped);
}

Result install(const Paths& inputPaths, const QString& launcher,
               bool forceDefault)
{
  const Paths paths = normalizedPaths(inputPaths);
  QString renderError;
  const QString rendered = desktopEntry(launcher, &renderError);
  if (rendered.isEmpty()) {
    return failure(Status::InvalidInput, launcher, renderError);
  }

  QLockFile lock(paths.lockFile);
  lock.setStaleLockTime(30'000);
  if (!lock.tryLock(LockTimeoutMs)) {
    return failure(Status::Busy, paths.lockFile,
                   QStringLiteral("another NXM integration update is active"));
  }

  Artifact current;
  Artifact legacy;
  Artifact historicalCurrent;
  Artifact wrapper;
  if (Result result = inspectArtifacts(paths, current, legacy,
                                       historicalCurrent, wrapper);
      !result.succeeded()) {
    return result;
  }
  if (Result result = preflightMimeFile(paths.mimeApps, MimeOperation::Install,
                                        forceDefault, legacy.owned, true);
      !result.succeeded()) {
    return result;
  }
  for (const QString& legacyMimePath : paths.legacyMimeApps) {
    if (Result result = preflightMimeFile(
            legacyMimePath, MimeOperation::Cleanup, false, legacy.owned,
            false);
        !result.succeeded()) {
      return result;
    }
  }

  bool changed = false;
  QString error;
  const QByteArray desktopBytes = rendered.toUtf8();
  const Artifact immediateDesktop =
      inspectDesktop(paths.desktop, false, paths.legacyWrapper);
  if (!immediateDesktop.missing && !immediateDesktop.owned) {
    return failure(Status::Collision, paths.desktop,
                   QStringLiteral("desktop entry changed after preflight"));
  }
  const Leaf existingDesktop = readRegularLeaf(paths.desktop, true);
  if (existingDesktop.kind != LeafKind::Regular ||
      existingDesktop.bytes != desktopBytes) {
    if (!writeAtomically(paths.desktop, desktopBytes,
                         QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                             QFileDevice::ReadGroup | QFileDevice::ReadOther,
                         error)) {
      return failure(Status::IoError, paths.desktop, error);
    }
    changed = true;
  }

  Result mimeResult = updateMimeFile(paths.mimeApps, MimeOperation::Install,
                                     forceDefault, legacy.owned, true);
  if (!mimeResult.succeeded()) {
    return mimeResult;
  }
  changed = changed || mimeResult.changed;

  for (const QString& legacyMimePath : paths.legacyMimeApps) {
    Result oldMimeResult = updateMimeFile(
        legacyMimePath, MimeOperation::Cleanup, false, legacy.owned, false);
    if (!oldMimeResult.succeeded()) {
      return oldMimeResult;
    }
    changed = changed || oldMimeResult.changed;
  }

  if (!removeOwned(paths.legacyDesktop, legacy, ArtifactType::LegacyDesktop,
                   paths.legacyWrapper, error)) {
    return failure(Status::IoError, paths.legacyDesktop, error);
  }
  if (legacy.owned) {
    changed = true;
  }
  if (!removeOwned(paths.historicalDesktop, historicalCurrent,
                   ArtifactType::CurrentDesktop, paths.legacyWrapper, error)) {
    return failure(Status::IoError, paths.historicalDesktop, error);
  }
  if (historicalCurrent.owned) {
    changed = true;
  }
  if (!removeOwned(paths.legacyWrapper, wrapper, ArtifactType::LegacyWrapper,
                   paths.legacyWrapper, error)) {
    return failure(Status::IoError, paths.legacyWrapper, error);
  }
  if (wrapper.owned) {
    changed = true;
  }
  Result result{changed ? Status::Success : Status::NoChange, changed};
  result.retiredLegacy = legacy.owned;
  return result;
}

Result uninstall(const Paths& inputPaths)
{
  const Paths paths = normalizedPaths(inputPaths);
  QLockFile lock(paths.lockFile);
  lock.setStaleLockTime(30'000);
  if (!lock.tryLock(LockTimeoutMs)) {
    return failure(Status::Busy, paths.lockFile,
                   QStringLiteral("another NXM integration update is active"));
  }

  Artifact current;
  Artifact legacy;
  Artifact historicalCurrent;
  Artifact wrapper;
  if (Result result = inspectArtifacts(paths, current, legacy,
                                       historicalCurrent, wrapper);
      !result.succeeded()) {
    return result;
  }
  const bool needsBlacklist = current.owned || historicalCurrent.owned ||
                              legacy.owned;
  if (Result result = preflightMimeFile(
          paths.mimeApps, MimeOperation::Uninstall, false, legacy.owned,
          needsBlacklist);
      !result.succeeded()) {
    return result;
  }
  for (const QString& legacyMimePath : paths.legacyMimeApps) {
    if (Result result = preflightMimeFile(
            legacyMimePath, MimeOperation::Cleanup, false, legacy.owned,
            false);
        !result.succeeded()) {
      return result;
    }
  }
  Result mimeResult = updateMimeFile(paths.mimeApps, MimeOperation::Uninstall,
                                     false, legacy.owned, needsBlacklist);
  if (!mimeResult.succeeded()) {
    return mimeResult;
  }
  bool changed = mimeResult.changed;
  for (const QString& legacyMimePath : paths.legacyMimeApps) {
    Result oldMimeResult = updateMimeFile(
        legacyMimePath, MimeOperation::Cleanup, false, legacy.owned, false);
    if (!oldMimeResult.succeeded()) {
      return oldMimeResult;
    }
    changed = changed || oldMimeResult.changed;
  }
  QString error;
  struct OwnedArtifact
  {
    QString path;
    Artifact artifact;
    ArtifactType type;
  };
  const OwnedArtifact owned[] = {
      {paths.desktop, current, ArtifactType::CurrentDesktop},
      {paths.historicalDesktop, historicalCurrent,
       ArtifactType::CurrentDesktop},
      {paths.legacyDesktop, legacy, ArtifactType::LegacyDesktop},
      {paths.legacyWrapper, wrapper, ArtifactType::LegacyWrapper},
  };
  for (const auto& ownedArtifact : owned) {
    if (!removeOwned(ownedArtifact.path, ownedArtifact.artifact,
                     ownedArtifact.type, paths.legacyWrapper, error)) {
      return failure(Status::IoError, ownedArtifact.path, error);
    }
    changed = changed || ownedArtifact.artifact.owned;
  }
  Result result{changed ? Status::Success : Status::NoChange, changed};
  result.retiredLegacy = legacy.owned;
  return result;
}

bool recognizesCompleteRegistration(const Paths& inputPaths)
{
  const Paths paths = normalizedPaths(inputPaths);
  Artifact current;
  Artifact legacy;
  Artifact historicalCurrent;
  Artifact wrapper;
  if (!inspectArtifacts(paths, current, legacy, historicalCurrent, wrapper)
           .succeeded()) {
    return false;
  }
  QStringList mimeCandidates{paths.mimeApps};
  mimeCandidates.append(paths.legacyMimeApps);
  for (const QString& mimePath : std::as_const(mimeCandidates)) {
    const Leaf mime = readRegularLeaf(mimePath, true);
    if (mime.kind != LeafKind::Regular) {
      continue;
    }
    const auto text = decodeUtf8(mime);
    if (!text) {
      continue;
    }
    if ((current.owned || historicalCurrent.owned) &&
        mimeHasActiveSchemes(*text, CurrentId, Schemes)) {
      return true;
    }
    const QStringList legacySchemes =
        legacy.supportsModl
            ? Schemes
            : QStringList{QStringLiteral("x-scheme-handler/nxm")};
    if (legacy.owned &&
        mimeHasActiveSchemes(*text, LegacyId, legacySchemes)) {
      return true;
    }
  }
  return false;
}

}  // namespace nxm_handler_integration

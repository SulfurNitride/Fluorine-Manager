/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "winesaverouting.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cerrno>

#ifdef Q_OS_UNIX
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace WineSaveRouting {
namespace {

constexpr qint64 MaximumIniBytes = 8 * 1024 * 1024;
constexpr qint64 MaximumReceiptBytes = 256 * 1024;

struct Line {
  QByteArray content;
  QByteArray ending;
};

struct VariantSnapshot {
  QString path;
  Value localPath;
  Value useMyGames;
  bool generalHeaderHadNoEnding{false};
};

struct Snapshot {
  QString owner;
  QString iniPath;
  QList<VariantSnapshot> variants;
};

Result fail(QString error, bool recoveryRequired = false) {
  return {false, std::move(error), recoveryRequired};
}

Result ok(bool recoveryRequired = false) {
  return {true, {}, recoveryRequired};
}

QString absolutePath(const QString &path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isWithin(const QString &rootPath, const QString &candidatePath) {
  const QString root = absolutePath(rootPath);
  const QString candidate = absolutePath(candidatePath);
  return !root.isEmpty() &&
         (candidate == root || candidate.startsWith(root + QDir::separator()));
}

QString canonicalWithMissingTail(const QString &path) {
  QFileInfo current(absolutePath(path));
  QStringList tail;
  while (!current.exists() && !current.isSymLink() &&
         !current.fileName().isEmpty()) {
    tail.prepend(current.fileName());
    current = QFileInfo(current.absolutePath());
  }
  QString resolved = current.canonicalFilePath();
  if (resolved.isEmpty())
    resolved = current.absoluteFilePath();
  for (const QString &component : tail)
    resolved = QDir(resolved).filePath(component);
  return QDir::cleanPath(resolved);
}

bool physicallyWithin(const QString &rootPath, const QString &candidatePath) {
  const QString root = canonicalWithMissingTail(rootPath);
  const QString candidate = canonicalWithMissingTail(candidatePath);
  const QString relative = QDir(root).relativeFilePath(candidate);
  return relative != QStringLiteral("..") &&
         !relative.startsWith(QStringLiteral("../")) &&
         !QDir::isAbsolutePath(relative);
}

Result resolvedRoute(const QString &iniPath, const QByteArray &route,
                     const QString &allowedRoot, QString &resolved) {
  resolved.clear();
  if (route.isEmpty() || route.size() > 32 * 1024 || route.contains('\0') ||
      route.contains('\r') || route.contains('\n') ||
      (!route.endsWith('\\') && !route.endsWith('/'))) {
    return fail(QStringLiteral("Invalid managed save route."));
  }

  const QString decoded = QString::fromUtf8(route);
  if (decoded.toUtf8() != route) {
    return fail(QStringLiteral("Managed save route is not valid UTF-8."));
  }
  QString normalized = decoded;
  normalized.replace('\\', '/');
  if (QDir::isAbsolutePath(normalized) ||
      normalized.contains(QChar(':')) ||
      QDir::cleanPath(normalized) == QStringLiteral(".")) {
    return fail(QStringLiteral("Managed save route must be relative."));
  }

  const QString iniParent = absolutePath(QFileInfo(iniPath).absolutePath());
  const QString lexicalResolved =
      absolutePath(QDir(iniParent).filePath(normalized));
  if (!allowedRoot.isEmpty() &&
      (!isWithin(allowedRoot, iniParent) ||
       !isWithin(allowedRoot, lexicalResolved) ||
       !physicallyWithin(allowedRoot, iniParent) ||
       !physicallyWithin(allowedRoot,
                         QFileInfo(lexicalResolved).absolutePath()))) {
    resolved.clear();
    return fail(QStringLiteral("Managed save route escapes its Wine prefix."));
  }
  resolved = lexicalResolved;
  return ok();
}

bool legacyRouteTargetsDirectory(const QString &iniPath, QByteArray route,
                                 const QString &saveDirectory) {
  route = route.trimmed();
  if (route.isEmpty() || route.contains('\0') || route.contains('\r') ||
      route.contains('\n')) {
    return false;
  }
  const QString decoded = QString::fromUtf8(route);
  if (decoded.toUtf8() != route)
    return false;
  QString normalized = decoded;
  normalized.replace('\\', '/');
  if (QDir::isAbsolutePath(normalized) || normalized.contains(QChar(':')))
    return false;

  const QString resolved = absolutePath(
      QDir(QFileInfo(iniPath).absolutePath()).filePath(normalized));
  const QString target = absolutePath(saveDirectory);
  return resolved.compare(target, Qt::CaseInsensitive) == 0 ||
         canonicalWithMissingTail(resolved) == canonicalWithMissingTail(target);
}

QStringList caseVariants(const QString &path) {
  const QFileInfo info(absolutePath(path));
  const QDir directory(info.absolutePath());
  QStringList result;
  for (const QString &entry :
       directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
                           QDir::Hidden | QDir::System)) {
    if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0)
      result.append(directory.filePath(entry));
  }
  return result;
}

Result effectiveCaseVariant(const QString &requestedFamily,
                            const QString &variant, QString &effective) {
  const QFileInfo info(variant);
  if (!info.isSymLink()) {
    effective = absolutePath(variant);
    return ok();
  }
  const QString target = info.symLinkTarget();
  const QFileInfo targetInfo(target);
  const QFileInfo familyInfo(absolutePath(requestedFamily));
  const bool sameFamily = !target.isEmpty() &&
                          QDir(info.absolutePath()).canonicalPath() ==
                              QDir(targetInfo.absolutePath()).canonicalPath() &&
                          targetInfo.fileName().compare(
                              familyInfo.fileName(), Qt::CaseInsensitive) == 0;
  if (sameFamily && !targetInfo.exists()) {
    effective.clear();
    return ok();
  }
  if (!sameFamily || targetInfo.isSymLink() || !targetInfo.isFile()) {
    return fail(
        QStringLiteral("Refusing unsafe INI case alias '%1'.").arg(variant));
  }
  effective = absolutePath(target);
  return ok();
}

bool safeLeaf(const QString &path, bool allowMissing, QString &detail) {
#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(path);
  struct stat status;
  if (::lstat(encoded.constData(), &status) == 0) {
    if (!S_ISREG(status.st_mode)) {
      detail = QStringLiteral("leaf is not a regular file");
      return false;
    }
    return true;
  }
  if (errno == ENOENT && allowMissing)
    return true;
  detail = QString::fromLocal8Bit(std::strerror(errno));
  return false;
#else
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink())
    return allowMissing;
  if (info.isSymLink() || !info.isFile()) {
    detail = QStringLiteral("leaf is not a regular file");
    return false;
  }
  return true;
#endif
}

Result readRegularFile(const QString &path, qint64 maximum, bool allowMissing,
                       QByteArray &bytes, bool &present) {
  bytes.clear();
  present = false;
  QString detail;
  if (!safeLeaf(path, allowMissing, detail)) {
    return fail(QStringLiteral("Refusing unsafe INI/receipt leaf '%1': %2")
                    .arg(path, detail));
  }

#ifdef Q_OS_UNIX
  const QByteArray encoded = QFile::encodeName(path);
  const int descriptor = ::open(encoded.constData(),
                                O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (descriptor < 0) {
    if (allowMissing && errno == ENOENT)
      return ok();
    return fail(QStringLiteral("Could not open '%1': %2")
                    .arg(path, QString::fromLocal8Bit(std::strerror(errno))));
  }
  struct stat status;
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 || status.st_size > maximum) {
    ::close(descriptor);
    return fail(
        QStringLiteral("Refusing invalid or oversized file '%1'.").arg(path));
  }
  QFile file;
  if (!file.open(descriptor, QIODevice::ReadOnly,
                 QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    return fail(QStringLiteral("Could not adopt '%1' for reading.").arg(path));
  }
#else
  if (!QFileInfo::exists(path))
    return ok();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 0 ||
      file.size() > maximum) {
    return fail(QStringLiteral("Could not safely read '%1'.").arg(path));
  }
#endif
  bytes = file.readAll();
  if (file.error() != QFileDevice::NoError || bytes.size() > maximum) {
    return fail(QStringLiteral("Could not completely read '%1'.").arg(path));
  }
  present = true;
  return ok();
}

QList<Line> splitLines(const QByteArray &bytes) {
  QList<Line> lines;
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    const qsizetype newline = bytes.indexOf('\n', offset);
    const qsizetype end = newline < 0 ? bytes.size() : newline;
    qsizetype contentEnd = end;
    if (contentEnd > offset && bytes[contentEnd - 1] == '\r')
      --contentEnd;
    lines.append(
        {bytes.mid(offset, contentEnd - offset),
         bytes.mid(contentEnd,
                   newline < 0 ? end - contentEnd : newline + 1 - contentEnd)});
    if (newline < 0)
      break;
    offset = newline + 1;
  }
  return lines;
}

QByteArray joinLines(const QList<Line> &lines) {
  QByteArray bytes;
  for (const Line &line : lines) {
    bytes += line.content;
    bytes += line.ending;
  }
  return bytes;
}

QByteArray defaultEnding(const QList<Line> &lines) {
  for (const Line &line : lines) {
    if (line.ending == "\r\n")
      return "\r\n";
    if (line.ending == "\n")
      return "\n";
  }
  return "\n";
}

struct Location {
  int sectionStart{-1};
  int sectionEnd{0};
  int keyLine{-1};
  int equals{-1};
};

QByteArray trimmedForParsing(const Line &line, int index) {
  QByteArray trimmed = line.content.trimmed();
  if (index == 0 && trimmed.startsWith("\xEF\xBB\xBF")) {
    trimmed.remove(0, 3);
    trimmed = trimmed.trimmed();
  }
  return trimmed;
}

Result locate(const QList<Line> &lines, const QByteArray &section,
              const QByteArray &key, Location &location) {
  const QByteArray header = '[' + section.toLower() + ']';
  int sections = 0;
  location = {-1, static_cast<int>(lines.size()), -1, -1};
  for (int index = 0; index < lines.size(); ++index) {
    const QByteArray trimmed = trimmedForParsing(lines[index], index);
    if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
      if (trimmed.toLower() == header) {
        ++sections;
        if (sections == 1) {
          location.sectionStart = index;
          location.sectionEnd = lines.size();
        }
      } else if (location.sectionStart >= 0 && sections == 1 &&
                 location.sectionEnd == lines.size()) {
        location.sectionEnd = index;
      }
    }
  }
  if (sections > 1) {
    return fail(QStringLiteral("INI contains duplicate [%1] sections.")
                    .arg(QString::fromLatin1(section)));
  }
  if (location.sectionStart < 0)
    return ok();

  for (int index = location.sectionStart + 1; index < location.sectionEnd;
       ++index) {
    const QByteArray trimmed = trimmedForParsing(lines[index], index);
    if (trimmed.isEmpty() || trimmed.startsWith(';') ||
        trimmed.startsWith('#')) {
      continue;
    }
    const int equals = lines[index].content.indexOf('=');
    if (equals <= 0)
      continue;
    if (lines[index].content.left(equals).trimmed().compare(
            key, Qt::CaseInsensitive) == 0) {
      if (location.keyLine >= 0) {
        return fail(
            QStringLiteral("INI contains duplicate key '%1' in [%2].")
                .arg(QString::fromLatin1(key), QString::fromLatin1(section)));
      }
      location.keyLine = index;
      location.equals = equals;
    }
  }
  return ok();
}

Result valueFromLines(const QList<Line> &lines, const QByteArray &section,
                      const QByteArray &key, Value &value) {
  Location location;
  Result result = locate(lines, section, key, location);
  if (!result)
    return result;
  value = {};
  if (location.keyLine >= 0) {
    value.present = true;
    value.bytes = lines[location.keyLine].content.mid(location.equals + 1);
  }
  return ok();
}

Result mutate(QList<Line> &lines, const QByteArray &section,
              const QByteArray &key, const Value &value) {
  Location location;
  Result result = locate(lines, section, key, location);
  if (!result)
    return result;
  if (!value.present) {
    if (location.keyLine >= 0)
      lines.removeAt(location.keyLine);
    return ok();
  }

  if (location.keyLine >= 0) {
    lines[location.keyLine].content =
        lines[location.keyLine].content.left(location.equals + 1) + value.bytes;
    return ok();
  }

  const QByteArray ending = defaultEnding(lines);
  if (location.sectionStart < 0) {
    if (!lines.isEmpty() && lines.last().ending.isEmpty()) {
      lines.last().ending = ending;
    }
    if (!lines.isEmpty() && !lines.last().content.trimmed().isEmpty()) {
      lines.append({{}, ending});
    }
    lines.append({'[' + section + ']', ending});
    lines.append({key + '=' + value.bytes, ending});
  } else {
    if (lines[location.sectionStart].ending.isEmpty()) {
      lines[location.sectionStart].ending = ending;
    }
    lines.insert(location.sectionStart + 1,
                 Line{key + '=' + value.bytes, ending});
  }
  return ok();
}

Result publish(const QString &path, const QByteArray &bytes,
               const QDateTime &modificationTime = {}) {
  const QString parent = QFileInfo(path).absolutePath();
  QString detail;
  if (!QFileInfo(parent).isDir() || QFileInfo(parent).isSymLink() ||
      !safeLeaf(path, /*allowMissing=*/true, detail)) {
    return fail(QStringLiteral("Refusing unsafe publication path '%1': %2")
                    .arg(path, detail));
  }
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) ||
      (!QFileInfo::exists(path) &&
       !file.setPermissions(QFileDevice::ReadOwner |
                            QFileDevice::WriteOwner)) ||
      file.write(bytes) != bytes.size() || !file.flush() ||
      (modificationTime.isValid() &&
       !file.setFileTime(modificationTime,
                         QFileDevice::FileModificationTime)) ||
      !file.commit()) {
    const QString error = file.errorString();
    file.cancelWriting();
    return fail(QStringLiteral("Could not atomically publish '%1': %2")
                    .arg(path, error));
  }
  return ok();
}

QJsonObject encodedValue(const Value &value) {
  return {
      {QStringLiteral("present"), value.present},
      {QStringLiteral("bytes"), QString::fromLatin1(value.bytes.toBase64())}};
}

Result decodedValue(const QJsonValue &json, Value &value) {
  if (!json.isObject())
    return fail(QStringLiteral("Invalid routing value receipt."));
  const QJsonObject object = json.toObject();
  if (!object.value(QStringLiteral("present")).isBool() ||
      !object.value(QStringLiteral("bytes")).isString()) {
    return fail(QStringLiteral("Invalid routing value receipt."));
  }
  value.present = object.value(QStringLiteral("present")).toBool();
  const QByteArray encoded =
      object.value(QStringLiteral("bytes")).toString().toLatin1();
  value.bytes =
      QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
  if (value.bytes.size() > 64 * 1024 || value.bytes.toBase64() != encoded ||
      (!value.present && !value.bytes.isEmpty())) {
    return fail(QStringLiteral("Invalid routing value size."));
  }
  return ok();
}

Result loadReceipt(const QString &iniPath, Snapshot &snapshot, bool &present) {
  QByteArray bytes;
  Result result = readRegularFile(receiptPathFor(iniPath), MaximumReceiptBytes,
                                  /*allowMissing=*/true, bytes, present);
  if (!result || !present)
    return result;
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return fail(QStringLiteral("Invalid save-routing receipt '%1'.")
                    .arg(receiptPathFor(iniPath)));
  }
  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != 2 ||
      !object.value(QStringLiteral("owner")).isString() ||
      !object.value(QStringLiteral("ini")).isString() ||
      !object.value(QStringLiteral("variants")).isArray()) {
    return fail(QStringLiteral("Invalid save-routing receipt identity."));
  }
  snapshot.owner = object.value(QStringLiteral("owner")).toString();
  snapshot.iniPath =
      QDir::cleanPath(object.value(QStringLiteral("ini")).toString());
  if (snapshot.owner.isEmpty() || snapshot.iniPath != absolutePath(iniPath)) {
    return fail(QStringLiteral("Save-routing receipt targets another INI."));
  }
  snapshot.variants.clear();
  for (const QJsonValue &value :
       object.value(QStringLiteral("variants")).toArray()) {
    if (!value.isObject())
      return fail(QStringLiteral("Invalid routing variant receipt."));
    const QJsonObject variant = value.toObject();
    if (!variant.value(QStringLiteral("path")).isString() ||
        !variant.value(QStringLiteral("generalHeaderHadNoEnding")).isBool()) {
      return fail(QStringLiteral("Invalid routing variant identity."));
    }
    VariantSnapshot decoded;
    decoded.path = absolutePath(variant.value(QStringLiteral("path")).toString());
    decoded.generalHeaderHadNoEnding =
        variant.value(QStringLiteral("generalHeaderHadNoEnding")).toBool();
    if (decoded.path.isEmpty() ||
        QFileInfo(decoded.path).absolutePath() !=
            QFileInfo(snapshot.iniPath).absolutePath() ||
        QFileInfo(decoded.path).fileName().compare(
            QFileInfo(snapshot.iniPath).fileName(), Qt::CaseInsensitive) != 0) {
      return fail(QStringLiteral("Routing variant escapes its INI family."));
    }
    result = decodedValue(variant.value(QStringLiteral("localSavePath")),
                          decoded.localPath);
    if (!result)
      return result;
    result = decodedValue(
        variant.value(QStringLiteral("useMyGamesDirectory")),
        decoded.useMyGames);
    if (!result)
      return result;
    if (std::any_of(snapshot.variants.cbegin(), snapshot.variants.cend(),
                    [&decoded](const VariantSnapshot &existing) {
                      return existing.path == decoded.path;
                    })) {
      return fail(QStringLiteral("Duplicate routing variant receipt."));
    }
    snapshot.variants.append(std::move(decoded));
  }
  if (snapshot.variants.isEmpty())
    return fail(QStringLiteral("Routing receipt has no INI variants."));
  return ok();
}

Result writeReceipt(const QString &iniPath, const Snapshot &snapshot) {
  QJsonArray variants;
  for (const VariantSnapshot &variant : snapshot.variants) {
    variants.append(QJsonObject{
        {QStringLiteral("path"), variant.path},
        {QStringLiteral("generalHeaderHadNoEnding"),
         variant.generalHeaderHadNoEnding},
        {QStringLiteral("localSavePath"), encodedValue(variant.localPath)},
        {QStringLiteral("useMyGamesDirectory"),
         encodedValue(variant.useMyGames)}});
  }
  const QJsonObject object{
      {QStringLiteral("version"), 2},
      {QStringLiteral("owner"), snapshot.owner},
      {QStringLiteral("ini"), snapshot.iniPath},
      {QStringLiteral("variants"), variants}};
  return publish(receiptPathFor(iniPath),
                 QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}

Result publishValues(const QString &iniPath, const Value &localPath,
                     const Value &useMyGames,
                     const QByteArray *expectedBytes = nullptr,
                     bool restoreHeaderEnding = false,
                     bool preserveModificationTime = false) {
  QByteArray bytes;
  bool present = false;
  const QDateTime originalModificationTime = QFileInfo(iniPath).lastModified();
  Result result = readRegularFile(iniPath, MaximumIniBytes,
                                  /*allowMissing=*/true, bytes, present);
  if (!result)
    return result;
  if (expectedBytes != nullptr && bytes != *expectedBytes) {
    return fail(QStringLiteral("INI changed before routing publication."));
  }
  QList<Line> lines = splitLines(bytes);
  result = mutate(lines, "General", "sLocalSavePath", localPath);
  if (!result)
    return result;
  result = mutate(lines, "General", "bUseMyGamesDirectory", useMyGames);
  if (!result)
    return result;
  if (restoreHeaderEnding) {
    Location location;
    result = locate(lines, "General", "__fluorine_missing_key__", location);
    if (!result)
      return result;
    if (location.sectionStart < 0 ||
        location.sectionStart != lines.size() - 1) {
      return fail(
          QStringLiteral("INI structure changed before routing restoration."));
    }
    lines[location.sectionStart].ending.clear();
  }
  return publish(iniPath, joinLines(lines),
                 preserveModificationTime ? originalModificationTime
                                          : QDateTime{});
}

} // namespace

QString receiptPathFor(const QString &iniPath) {
  const QFileInfo info(absolutePath(iniPath));
  return QDir(info.absolutePath())
      .filePath(QStringLiteral(".%1.fluorine-save-routing.json")
                    .arg(info.fileName()));
}

Result readValue(const QString &iniPath, const QByteArray &section,
                 const QByteArray &key, Value &value) {
  QByteArray bytes;
  bool present = false;
  Result result = readRegularFile(iniPath, MaximumIniBytes,
                                  /*allowMissing=*/true, bytes, present);
  if (!result)
    return result;
  return valueFromLines(splitLines(bytes), section, key, value);
}

Result routeFor(const QString &iniPath, const QString &saveDirectory,
                const QString &allowedRoot, const QByteArray &explicitRoute,
                QByteArray &route) {
  route.clear();
  const QString target = absolutePath(saveDirectory);
  if (!isWithin(allowedRoot, target)) {
    return fail(QStringLiteral("Managed save target escapes its Wine prefix."));
  }

  QByteArray candidate = explicitRoute;
  if (candidate.isEmpty()) {
    const QString iniParent = absolutePath(QFileInfo(iniPath).absolutePath());
    QString relative = QDir(iniParent).relativeFilePath(target);
    relative = QDir::cleanPath(relative);
    relative.replace('/', '\\');
    if (!relative.endsWith('\\'))
      relative.append('\\');
    candidate = relative.toUtf8();
  }

  QString resolved;
  Result result = resolvedRoute(iniPath, candidate, allowedRoot, resolved);
  if (!result)
    return result;
  if (resolved != target) {
    return fail(QStringLiteral("Managed save route targets another directory."));
  }
  route = candidate;
  return ok();
}

Result routeTargetsDirectory(const QString &iniPath, const QByteArray &route,
                             const QString &saveDirectory,
                             const QString &allowedRoot, bool &matches) {
  matches = false;
  QString configured = QString::fromUtf8(route.trimmed());
  configured.replace('\\', '/');
  if (QDir::isAbsolutePath(configured) || configured.contains(QChar(':')))
    return ok();
  QString resolved;
  const Result result = resolvedRoute(iniPath, route.trimmed(), allowedRoot,
                                      resolved);
  if (!result)
    return result;
  const QString target = absolutePath(saveDirectory);
  if (resolved.compare(target, Qt::CaseInsensitive) == 0) {
    matches = true;
    return ok();
  }
  if (canonicalWithMissingTail(resolved) == canonicalWithMissingTail(target)) {
    matches = true;
    return ok();
  }
  if (!allowedRoot.isEmpty() && !physicallyWithin(allowedRoot, resolved)) {
    return fail(QStringLiteral("Configured save route escapes its Wine prefix."));
  }
  return ok();
}

Result familyTargetsDirectory(const QString &iniPath,
                              const QString &saveDirectory,
                              const QString &allowedRoot, bool &matches) {
  matches = false;
  QStringList effectiveVariants;
  for (const QString &variant : caseVariants(iniPath)) {
    QString effective;
    Result result = effectiveCaseVariant(iniPath, variant, effective);
    if (!result)
      return result;
    if (effective.isEmpty() || effectiveVariants.contains(effective))
      continue;
    effectiveVariants.append(effective);

    Value localPath;
    result = readValue(effective, "General", "sLocalSavePath", localPath);
    if (!result)
      return result;
    if (!localPath.present)
      continue;
    bool variantMatches = false;
    result = routeTargetsDirectory(effective, localPath.bytes, saveDirectory,
                                   allowedRoot, variantMatches);
    if (!result)
      return result;
    if (variantMatches) {
      matches = true;
      return ok();
    }
  }
  return ok();
}

Result restoreConfirmedLegacyReceipt(const QString &iniPath,
                                     const QString &receiptPath,
                                     const QString &routeContextIni,
                                     const QByteArray &managedRoute,
                                     const QString &saveDirectory,
                                     const QString &allowedRoot) {
  bool managedRouteValid = false;
  Result result = routeTargetsDirectory(routeContextIni, managedRoute,
                                        saveDirectory, allowedRoot,
                                        managedRouteValid);
  if (!result)
    return result;
  if (!managedRouteValid) {
    return fail(QStringLiteral("Legacy recovery route does not target the "
                               "managed save directory."));
  }

  QByteArray receiptBytes;
  bool receiptPresent = false;
  result = readRegularFile(receiptPath, MaximumReceiptBytes,
                           /*allowMissing=*/false, receiptBytes,
                           receiptPresent);
  if (!result || !receiptPresent)
    return result ? fail(QStringLiteral("Legacy save receipt is missing."))
                  : result;

  const QList<Line> receiptLines = splitLines(receiptBytes);
  Value savedLocalPath;
  Value savedUseMyGames;
  result = valueFromLines(receiptLines, "General", "sLocalSavePath",
                          savedLocalPath);
  if (!result)
    return result;
  result = valueFromLines(receiptLines, "General", "bUseMyGamesDirectory",
                          savedUseMyGames);
  if (!result)
    return result;
  if (!savedLocalPath.present && !savedUseMyGames.present) {
    return fail(QStringLiteral("Legacy save receipt contains no restorable "
                               "routing values."));
  }

  auto sameValue = [](const Value &left, const Value &right) {
    return left.present == right.present &&
           (!left.present || left.bytes == right.bytes);
  };
  if (savedLocalPath.present) {
    if (legacyRouteTargetsDirectory(routeContextIni, savedLocalPath.bytes,
                                    saveDirectory)) {
      return fail(QStringLiteral("Legacy receipt saved the managed route as "
                                 "its original value; preserving it for "
                                 "manual recovery."));
    }
  }

  struct PendingRestore {
    QString path;
    QByteArray expected;
  };
  PendingRestore pending;
  bool needsRestore = false;

  // The legacy receipt was written for one exact INI spelling. It contains no
  // case-family manifest, so it cannot authorize rewriting a distinct sibling
  // that happens to contain the same temporary route. Resolve only a strict
  // same-directory case alias of the selected path and reject another managed
  // real variant as ambiguous.
  QString selected;
  result = effectiveCaseVariant(iniPath, iniPath, selected);
  if (!result)
    return result;
  if (selected.isEmpty()) {
    return fail(QStringLiteral("Legacy routing target is missing."));
  }

  QStringList effectiveVariants;
  for (const QString &variant : caseVariants(iniPath)) {
    QString effective;
    result = effectiveCaseVariant(iniPath, variant, effective);
    if (!result)
      return result;
    if (effective.isEmpty() || effectiveVariants.contains(effective))
      continue;
    effectiveVariants.append(effective);

    if (effective == selected)
      continue;

    Value siblingLocalPath;
    result = readValue(effective, "General", "sLocalSavePath",
                       siblingLocalPath);
    if (!result)
      return result;
    bool siblingTargetsManaged = false;
    if (siblingLocalPath.present) {
      result = routeTargetsDirectory(routeContextIni, siblingLocalPath.bytes,
                                     saveDirectory, allowedRoot,
                                     siblingTargetsManaged);
      if (!result)
        return result;
    }
    if (siblingTargetsManaged) {
      return fail(QStringLiteral("Legacy routing receipt is ambiguous because "
                                 "distinct INI '%1' also selects the managed "
                                 "route.")
                      .arg(effective));
    }
  }

  QByteArray currentBytes;
  bool currentPresent = false;
  result = readRegularFile(selected, MaximumIniBytes,
                           /*allowMissing=*/false, currentBytes,
                           currentPresent);
  if (!result || !currentPresent)
    return result ? fail(QStringLiteral("Legacy routing target is missing."))
                  : result;
  const QList<Line> currentLines = splitLines(currentBytes);
  Value currentLocalPath;
  Value currentUseMyGames;
  result = valueFromLines(currentLines, "General", "sLocalSavePath",
                          currentLocalPath);
  if (!result)
    return result;
  result = valueFromLines(currentLines, "General", "bUseMyGamesDirectory",
                          currentUseMyGames);
  if (!result)
    return result;

  const bool localRestored = sameValue(currentLocalPath, savedLocalPath);
  const bool useRestored = sameValue(currentUseMyGames, savedUseMyGames);
  if (!localRestored || !useRestored) {
    bool localActive = false;
    if (currentLocalPath.present) {
      result = routeTargetsDirectory(routeContextIni, currentLocalPath.bytes,
                                     saveDirectory, allowedRoot, localActive);
      if (!result)
        return result;
    }
    const bool useActive = currentUseMyGames.present &&
                           currentUseMyGames.bytes.trimmed() == "1";
    if ((localActive || localRestored) && (useActive || useRestored) &&
        (localActive || useActive)) {
      pending = {selected, currentBytes};
      needsRestore = true;
    } else if (localActive || useActive) {
      return fail(QStringLiteral("Legacy routing target '%1' conflicts with "
                                 "its saved values.")
                      .arg(selected));
    } else {
      return fail(QStringLiteral("The selected INI does not contain the legacy "
                                 "managed route or its saved values."));
    }
  }

  if (needsRestore) {
    result = publishValues(pending.path, savedLocalPath, savedUseMyGames,
                           &pending.expected);
    if (!result) {
      result.recoveryRequired = true;
      return result;
    }
  }

  QString detail;
  if (!safeLeaf(receiptPath, /*allowMissing=*/false, detail) ||
      !QFile::remove(receiptPath)) {
    return fail(QStringLiteral("Routing values were restored, but legacy "
                               "receipt '%1' could not be retired: %2")
                    .arg(receiptPath, detail),
                true);
  }
  return ok();
}

Result clearConfirmedLegacyRoute(const QString &iniPath,
                                 const QString &routeContextIni,
                                 const QByteArray &managedRoute,
                                 const QString &saveDirectory,
                                 const QString &allowedRoot) {
  bool expectedTargetsManaged = false;
  Result result = routeTargetsDirectory(routeContextIni, managedRoute,
                                        saveDirectory, allowedRoot,
                                        expectedTargetsManaged);
  if (!result)
    return result;
  if (!expectedTargetsManaged) {
    return fail(QStringLiteral("Legacy recovery route does not target the "
                               "managed save directory."));
  }

  QString selected;
  result = effectiveCaseVariant(iniPath, iniPath, selected);
  if (!result)
    return result;
  if (selected.isEmpty())
    return fail(QStringLiteral("Legacy routing target is missing."));

  QStringList effectiveVariants;
  for (const QString &variant : caseVariants(iniPath)) {
    QString effective;
    result = effectiveCaseVariant(iniPath, variant, effective);
    if (!result)
      return result;
    if (effective.isEmpty() || effectiveVariants.contains(effective))
      continue;
    effectiveVariants.append(effective);
    if (effective == selected)
      continue;

    Value siblingLocalPath;
    result = readValue(effective, "General", "sLocalSavePath",
                       siblingLocalPath);
    if (!result)
      return result;
    bool siblingTargetsManaged = false;
    if (siblingLocalPath.present) {
      result = routeTargetsDirectory(routeContextIni, siblingLocalPath.bytes,
                                     saveDirectory, allowedRoot,
                                     siblingTargetsManaged);
      if (!result)
        return result;
    }
    if (siblingTargetsManaged) {
      return fail(QStringLiteral("Legacy routing state is ambiguous because "
                                 "distinct INI '%1' also selects the managed "
                                 "route.")
                      .arg(effective));
    }
  }

  QByteArray currentBytes;
  bool currentPresent = false;
  result = readRegularFile(selected, MaximumIniBytes,
                           /*allowMissing=*/false, currentBytes,
                           currentPresent);
  if (!result || !currentPresent)
    return result ? fail(QStringLiteral("Legacy routing target is missing."))
                  : result;
  const QList<Line> lines = splitLines(currentBytes);
  Value localPath;
  Value useMyGames;
  result = valueFromLines(lines, "General", "sLocalSavePath", localPath);
  if (!result)
    return result;
  result = valueFromLines(lines, "General", "bUseMyGamesDirectory",
                          useMyGames);
  if (!result)
    return result;

  bool currentTargetsManaged = false;
  if (localPath.present) {
    result = routeTargetsDirectory(routeContextIni, localPath.bytes,
                                   saveDirectory, allowedRoot,
                                   currentTargetsManaged);
    if (!result)
      return result;
  }
  if (!currentTargetsManaged || !useMyGames.present ||
      useMyGames.bytes.trimmed() != "1") {
    return fail(QStringLiteral("Receipt-less legacy recovery requires the "
                               "exact managed routing pair."));
  }

  result = publishValues(selected, Value{}, Value{}, &currentBytes);
  if (!result)
    result.recoveryRequired = true;
  return result;
}

Result pendingOwner(const QString &iniPath, QString &ownerId, bool &present) {
  Snapshot snapshot;
  Result result = loadReceipt(iniPath, snapshot, present);
  if (result && present)
    ownerId = snapshot.owner;
  else
    ownerId.clear();
  return result;
}

Result activate(const QString &iniPath, const QString &ownerId,
                const QByteArray &route, const QString &saveDirectory,
                const QString &allowedRoot) {
  if (ownerId.trimmed().isEmpty())
    return fail(QStringLiteral("Empty routing owner."));
  QByteArray validatedRoute;
  Result result = routeFor(iniPath, saveDirectory, allowedRoot, route,
                           validatedRoute);
  if (!result)
    return result;
  Snapshot existing;
  bool receiptPresent = false;
  result = loadReceipt(iniPath, existing, receiptPresent);
  if (!result)
    return result;
  if (receiptPresent) {
    return fail(
        QStringLiteral("Save-routing receipt already exists for owner '%1'.")
            .arg(existing.owner));
  }

  Snapshot snapshot;
  snapshot.owner = ownerId;
  snapshot.iniPath = absolutePath(iniPath);
  QStringList effectiveVariants;
  for (const QString &variant : caseVariants(iniPath)) {
    QString effective;
    result = effectiveCaseVariant(iniPath, variant, effective);
    if (!result)
      return result;
    if (!effective.isEmpty() && !effectiveVariants.contains(effective))
      effectiveVariants.append(effective);
  }
  if (effectiveVariants.isEmpty())
    effectiveVariants.append(snapshot.iniPath);

  QList<QByteArray> originals;
  for (const QString &variantPath : effectiveVariants) {
    QByteArray original;
    bool iniPresent = false;
    result = readRegularFile(variantPath, MaximumIniBytes,
                             /*allowMissing=*/true, original, iniPresent);
    if (!result)
      return result;
    const QList<Line> lines = splitLines(original);
    VariantSnapshot variant;
    variant.path = variantPath;
    Location general;
    result = locate(lines, "General", "__fluorine_missing_key__", general);
    if (!result)
      return result;
    variant.generalHeaderHadNoEnding =
        general.sectionStart >= 0 &&
        lines[general.sectionStart].ending.isEmpty();
    result = valueFromLines(lines, "General", "sLocalSavePath",
                            variant.localPath);
    if (!result)
      return result;
    result = valueFromLines(lines, "General", "bUseMyGamesDirectory",
                            variant.useMyGames);
    if (!result)
      return result;
    snapshot.variants.append(std::move(variant));
    originals.append(std::move(original));
  }

  for (const VariantSnapshot &variant : snapshot.variants) {
    if (!variant.localPath.present)
      continue;
    bool alreadyManaged = false;
    result = routeTargetsDirectory(variant.path, variant.localPath.bytes,
                                   saveDirectory, allowedRoot,
                                   alreadyManaged);
    if (!result)
      return result;
    if (alreadyManaged) {
      return fail(QStringLiteral("Refusing to adopt an unowned managed save "
                                 "route from '%1'.")
                      .arg(variant.path));
    }
  }

  result = writeReceipt(iniPath, snapshot);
  if (!result)
    return result;

  for (qsizetype index = 0; index < snapshot.variants.size(); ++index) {
    result = publishValues(snapshot.variants[index].path,
                           Value{true, validatedRoute}, Value{true, "1"},
                           &originals[index],
                           /*restoreHeaderEnding=*/false,
                           /*preserveModificationTime=*/true);
    if (!result) {
      result.recoveryRequired = true;
      return result;
    }
  }
  return ok(/*recoveryRequired=*/true);
}

Result restore(const QString &iniPath, const QString &expectedOwner) {
  Snapshot snapshot;
  bool present = false;
  Result result = loadReceipt(iniPath, snapshot, present);
  if (!result)
    return result;
  if (!present) {
    return expectedOwner.isEmpty()
               ? ok()
               : fail(QStringLiteral("Save-routing receipt is missing."), true);
  }
  if (!expectedOwner.isEmpty() && snapshot.owner != expectedOwner) {
    return fail(
        QStringLiteral("Save-routing receipt belongs to another launch."),
        true);
  }
  QStringList variants = caseVariants(iniPath);
  const QString exact = absolutePath(iniPath);
  QStringList effectiveVariants;
  for (const QString &variant : variants) {
    QString effective;
    result = effectiveCaseVariant(iniPath, variant, effective);
    if (!result) {
      result.recoveryRequired = true;
      return result;
    }
    if (!effective.isEmpty() && !effectiveVariants.contains(effective))
      effectiveVariants.append(effective);
  }
  for (const QString &variant : effectiveVariants) {
    const VariantSnapshot *original = nullptr;
    for (const VariantSnapshot &candidate : snapshot.variants) {
      if (candidate.path == variant) {
        original = &candidate;
        break;
      }
    }
    if (original == nullptr) {
      for (const VariantSnapshot &candidate : snapshot.variants) {
        if (candidate.path == exact) {
          original = &candidate;
          break;
        }
      }
    }
    if (original == nullptr)
      original = &snapshot.variants.first();
    result = publishValues(variant, original->localPath, original->useMyGames,
                           nullptr,
                           variant == original->path &&
                               original->generalHeaderHadNoEnding,
                           /*preserveModificationTime=*/true);
    if (!result) {
      result.recoveryRequired = true;
      return result;
    }
  }
  const QString receipt = receiptPathFor(iniPath);
  QString detail;
  if (!safeLeaf(receipt, /*allowMissing=*/false, detail) ||
      !QFile::remove(receipt)) {
    return fail(QStringLiteral("Could not retire save-routing receipt '%1'.")
                    .arg(receipt),
                true);
  }
  return ok();
}

} // namespace WineSaveRouting

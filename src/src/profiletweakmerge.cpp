#include "profiletweakmerge.h"

#include <uibase/transactionalwritefile.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QObject>

namespace ProfileTweakMerge {
namespace {

struct IniValue {
  QString key;
  QString value;
};

struct IniSection {
  QString name;
  QList<IniValue> values;
};

bool containsControl(const QString &value) {
  return value.contains(QChar::Null) || value.contains('\r') ||
         value.contains('\n');
}

bool validSection(const QString &section) {
  return !section.isEmpty() && section == section.trimmed() &&
         !section.contains('[') && !section.contains(']') &&
         !containsControl(section);
}

bool validKey(const QString &key) {
  return !key.isEmpty() && key == key.trimmed() && !key.contains('=') &&
         !containsControl(key);
}

IniSection *findSection(QList<IniSection> &sections, const QString &name) {
  for (IniSection &section : sections) {
    if (section.name.compare(name, Qt::CaseInsensitive) == 0) {
      return &section;
    }
  }
  return nullptr;
}

void setValue(QList<IniSection> &sections, const QString &sectionName,
              const QString &key, const QString &value) {
  IniSection *section = findSection(sections, sectionName);
  if (section == nullptr) {
    sections.append({sectionName, {}});
    section = &sections.last();
  }
  for (IniValue &existing : section->values) {
    if (existing.key.compare(key, Qt::CaseInsensitive) == 0) {
      existing.value = value;
      return;
    }
  }
  section->values.append({key, value});
}

bool mergeFile(const QString &path, QList<IniSection> &sections,
               QString &error) {
  QFile source(path);
  if (!source.open(QIODevice::ReadOnly)) {
    if (!QFileInfo::exists(path)) {
      return true;
    }
    error = QObject::tr("Tweak file '%1' could not be opened: %2")
                .arg(path, source.errorString());
    return false;
  }
  const QByteArray bytes = source.readAll();
  if (source.error() != QFileDevice::NoError) {
    error = QObject::tr("Tweak file '%1' could not be read: %2")
                .arg(path, source.errorString());
    return false;
  }

  QString text = QString::fromUtf8(bytes);
  if (text.toUtf8() != bytes) {
    error = QObject::tr("Tweak file '%1' is not valid UTF-8.").arg(path);
    return false;
  }
  if (!text.isEmpty() && text.front() == QChar::ByteOrderMark) {
    text.removeFirst();
  }
  QString currentSection;
  qsizetype lineNumber = 0;
  for (const QString &rawLine : text.split('\n')) {
    ++lineNumber;
    QString line = rawLine;
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) {
      continue;
    }
    if (line.startsWith('[') && line.endsWith(']')) {
      currentSection = line.mid(1, line.size() - 2).trimmed();
      if (!validSection(currentSection)) {
        error =
            QObject::tr("Tweak file '%1' has an invalid section on line %2.")
                .arg(path)
                .arg(lineNumber);
        return false;
      }
      continue;
    }
    const qsizetype equals = line.indexOf('=');
    if (equals <= 0 || currentSection.isEmpty()) {
      continue;
    }
    const QString key = line.left(equals).trimmed();
    const QString value = line.mid(equals + 1).trimmed();
    if (!validKey(key) || containsControl(value)) {
      error = QObject::tr("Tweak file '%1' has an invalid value on line %2.")
                  .arg(path)
                  .arg(lineNumber);
      return false;
    }
    setValue(sections, currentSection, key, value);
  }
  return true;
}

QByteArray serialize(const QList<IniSection> &sections) {
  QByteArray bytes;
  for (qsizetype sectionIndex = 0; sectionIndex < sections.size();
       ++sectionIndex) {
    const IniSection &section = sections[sectionIndex];
    if (sectionIndex != 0) {
      bytes.append('\n');
    }
    bytes.append('[').append(section.name.toUtf8()).append("]\n");
    for (const IniValue &value : section.values) {
      bytes.append(value.key.toUtf8())
          .append('=')
          .append(value.value.toUtf8())
          .append('\n');
    }
  }
  return bytes;
}

} // namespace

bool publish(const QStringList &tweakFiles, const QString &targetPath,
             QString *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (targetPath.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QObject::tr("The tweaked INI destination is empty.");
    }
    return false;
  }

  MOBase::TransactionalWriteFile transaction(targetPath);
  QByteArray original;
  bool present = false;
  if (!transaction.readOriginal(original, present)) {
    if (error != nullptr) {
      *error = transaction.errorString();
    }
    return false;
  }

  QList<IniSection> sections;
  QString mergeError;
  for (const QString &tweakFile : tweakFiles) {
    if (!mergeFile(tweakFile, sections, mergeError)) {
      if (error != nullptr) {
        *error = mergeError;
      }
      return false;
    }
  }
  setValue(sections, QStringLiteral("Archive"),
           QStringLiteral("bInvalidateOlderFiles"), QStringLiteral("1"));

  const QByteArray replacement = serialize(sections);
  if (!transaction.replaceWith(replacement)) {
    if (error != nullptr) {
      *error = transaction.errorString();
    }
    return false;
  }
  return true;
}

} // namespace ProfileTweakMerge

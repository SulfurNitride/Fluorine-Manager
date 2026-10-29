#include "openmwsettings.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace OpenMWSettings
{

namespace
{
const QRegularExpression& integerPattern()
{
  static const QRegularExpression re(QStringLiteral("^-?\\d+$"));
  return re;
}

const QRegularExpression& floatPattern()
{
  static const QRegularExpression re(
      QStringLiteral("^-?(\\d+\\.\\d*|\\.\\d+)([eE][-+]?\\d+)?$"));
  return re;
}

Type inferType(const QString& value)
{
  if (value == QLatin1String("true") || value == QLatin1String("false"))
    return Type::Bool;
  if (integerPattern().match(value).hasMatch())
    return Type::Int;
  if (floatPattern().match(value).hasMatch())
    return Type::Float;
  return Type::Text;
}

// "0 = Fullscreen, 1 = Windowed Fullscreen, 2 = Windowed" and the same spread
// over several comment lines.
QList<Choice> numberedChoices(const QString& comment, const QString& defaultValue)
{
  static const QRegularExpression re(
      QStringLiteral("(?:^|[\\s(,])(-?\\d+)\\s*=\\s*([A-Za-z][^,\\n.;)=]*)"));
  QList<Choice> found;
  QSet<QString> seen;
  auto it = re.globalMatch(comment);
  while (it.hasNext()) {
    const auto m = it.next();
    const QString value = m.captured(1);
    if (seen.contains(value))
      continue;
    seen.insert(value);
    found.append({value, QStringLiteral("%1 — %2").arg(value, m.captured(2).trimmed())});
  }
  if (found.size() < 2 || !seen.contains(defaultValue))
    return {};
  return found;
}

// "(nearest or linear)" / "(none, nearest, or linear)".
QList<Choice> wordChoices(const QString& comment, const QString& defaultValue)
{
  static const QRegularExpression re(QStringLiteral(
      "\\(([A-Za-z0-9_]+(?:\\s*,\\s*[A-Za-z0-9_]+)*(?:,?\\s*or\\s+[A-Za-z0-9_]+)?)\\)"));
  auto it = re.globalMatch(comment);
  while (it.hasNext()) {
    const auto m = it.next();
    QString text = m.captured(1);
    text.replace(QRegularExpression(QStringLiteral(",?\\s+or\\s+")), QStringLiteral(","));
    QList<Choice> found;
    for (const QString& word : text.split(',', Qt::SkipEmptyParts)) {
      const QString value = word.trimmed();
      if (!value.isEmpty())
        found.append({value, value});
    }
    if (found.size() < 2)
      continue;
    for (const auto& c : found)
      if (c.value == defaultValue)
        return found;
  }
  return {};
}

bool isKeyLine(const QString& line, QString* key, QString* value)
{
  const QString trimmed = line.trimmed();
  if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith('['))
    return false;
  const int eq = trimmed.indexOf('=');
  if (eq <= 0)
    return false;
  *key   = trimmed.left(eq).trimmed();
  *value = trimmed.mid(eq + 1).trimmed();
  return !key->isEmpty();
}

bool isSectionLine(const QString& line, QString* section)
{
  const QString trimmed = line.trimmed();
  if (!trimmed.startsWith('[') || !trimmed.contains(']'))
    return false;
  *section = trimmed.mid(1, trimmed.indexOf(']') - 1).trimmed();
  return true;
}
}  // namespace

QString Setting::label() const
{
  if (key.isEmpty())
    return key;
  return key.left(1).toUpper() + key.mid(1);
}

Schema Schema::parse(const QString& defaultsText)
{
  Schema schema;
  QString section;
  QStringList pending;
  bool previousWasComment = false;
  bool previousWasKey     = false;
  for (const QString& line : defaultsText.split('\n')) {
    QString name, value;
    if (isSectionLine(line, &name)) {
      section = name;
      pending.clear();
      previousWasComment = previousWasKey = false;
    } else if (line.trimmed().startsWith('#')) {
      // A new comment block starts unless this line continues the last one.
      if (!previousWasComment)
        pending.clear();
      QString text = line.trimmed().mid(1);
      if (text.startsWith(' '))
        text.remove(0, 1);
      pending.append(text);
      previousWasComment = true;
      previousWasKey     = false;
    } else if (line.trimmed().isEmpty()) {
      // A blank line after a setting ends its comment; after a comment it
      // keeps it for the setting that follows.
      if (previousWasKey)
        pending.clear();
      previousWasComment = false;
    } else if (isKeyLine(line, &name, &value) && !section.isEmpty()) {
      Setting setting;
      setting.section      = section;
      setting.key          = name;
      setting.defaultValue = value;
      setting.comment      = pending.join(QLatin1Char('\n')).trimmed();
      setting.type         = inferType(value);
      if (setting.type == Type::Int)
        setting.choices = numberedChoices(setting.comment, value);
      else if (setting.type == Type::Text)
        setting.choices = wordChoices(setting.comment, value);
      schema.m_settings.append(setting);
      previousWasComment = false;
      previousWasKey     = true;
    }
  }
  return schema;
}

Schema Schema::bundled()
{
  QFile file(QStringLiteral(":/openmw/settings-default.cfg"));
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return parse(QString::fromUtf8(file.readAll()));
}

const Setting* Schema::find(const QString& section, const QString& key) const
{
  for (const auto& s : m_settings)
    if (s.section == section && s.key == key)
      return &s;
  return nullptr;
}

QStringList Schema::sections() const
{
  QStringList out;
  for (const auto& s : m_settings)
    if (!out.contains(s.section))
      out.append(s.section);
  return out;
}

bool File::load(const QString& path, QString* error)
{
  m_path = path;
  m_lines.clear();
  m_modified = false;
  QFile file(path);
  if (!file.exists()) {
    reindex();
    return true;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    reindex();
    return false;
  }
  QString text = QString::fromUtf8(file.readAll());
  text.remove('\r');
  m_lines = text.split('\n');
  // A trailing newline yields one empty last element; it is re-added on save.
  if (!m_lines.isEmpty() && m_lines.last().isEmpty())
    m_lines.removeLast();
  reindex();
  return true;
}

void File::reindex()
{
  m_index.clear();
  m_sectionEnd.clear();
  QString section;
  for (int i = 0; i < m_lines.size(); ++i) {
    QString name, value;
    if (isSectionLine(m_lines[i], &name)) {
      section = name;
      m_sectionEnd[section] = i + 1;
    } else if (isKeyLine(m_lines[i], &name, &value)) {
      m_index[section + QLatin1Char('/') + name] = i;
      m_sectionEnd[section]                      = i + 1;
    }
  }
}

bool File::contains(const QString& section, const QString& key) const
{
  return m_index.contains(section + QLatin1Char('/') + key);
}

QString File::value(const QString& section, const QString& key) const
{
  const auto it = m_index.constFind(section + QLatin1Char('/') + key);
  if (it == m_index.constEnd())
    return {};
  QString name, value;
  isKeyLine(m_lines[it.value()], &name, &value);
  return value;
}

void File::setValue(const QString& section, const QString& key, const QString& value)
{
  const QString id = section + QLatin1Char('/') + key;
  const auto it    = m_index.constFind(id);
  if (it != m_index.constEnd()) {
    QString name, current;
    isKeyLine(m_lines[it.value()], &name, &current);
    if (current == value)
      return;
    m_lines[it.value()] = QStringLiteral("%1 = %2").arg(name, value);
  } else if (m_sectionEnd.contains(section)) {
    m_lines.insert(m_sectionEnd.value(section), QStringLiteral("%1 = %2").arg(key, value));
  } else {
    if (!m_lines.isEmpty() && !m_lines.last().trimmed().isEmpty())
      m_lines.append(QString());
    m_lines.append(QStringLiteral("[%1]").arg(section));
    m_lines.append(QStringLiteral("%1 = %2").arg(key, value));
  }
  m_modified = true;
  reindex();
}

void File::remove(const QString& section, const QString& key)
{
  const auto it = m_index.constFind(section + QLatin1Char('/') + key);
  if (it == m_index.constEnd())
    return;
  m_lines.removeAt(it.value());
  m_modified = true;
  reindex();
}

QList<QPair<QString, QString>> File::entries() const
{
  QList<QPair<QString, QString>> out;
  QString section;
  for (const QString& line : m_lines) {
    QString name, value;
    if (isSectionLine(line, &name))
      section = name;
    else if (isKeyLine(line, &name, &value))
      out.append({section, name});
  }
  return out;
}

bool File::save(QString* error)
{
  QSaveFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  file.write(text().toUtf8());
  if (!file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  m_modified = false;
  return true;
}

}  // namespace OpenMWSettings

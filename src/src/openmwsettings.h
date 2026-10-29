#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Data layer of the OpenMW settings editor: what settings exist (taken from
// OpenMW's own defaults file) and a lossless editor for a settings.cfg.
namespace OpenMWSettings
{

enum class Type
{
  Bool,
  Int,
  Float,
  Text
};

struct Choice
{
  QString value;
  QString label;
};

struct Setting
{
  QString section;
  QString key;
  QString defaultValue;
  // The explanation written above the setting in OpenMW's defaults file.
  QString comment;
  Type type{Type::Text};
  // A closed set of values read from the comment ("0 = Off, 1 = On",
  // "(nearest or linear)"). Empty for free-form settings.
  QList<Choice> choices;

  QString id() const { return section + QLatin1Char('/') + key; }
  // "window mode" -> "Window mode".
  QString label() const;
};

class Schema
{
public:
  // Parse OpenMW's settings-default.cfg.
  static Schema parse(const QString& defaultsText);
  // The defaults bundled with Fluorine (OpenMW 0.51).
  static Schema bundled();

  const QList<Setting>& settings() const { return m_settings; }
  const Setting* find(const QString& section, const QString& key) const;
  QStringList sections() const;

private:
  QList<Setting> m_settings;
};

// A settings.cfg held as lines, so everything this editor does not touch
// (comments, ordering, keys it does not know) is written back unchanged.
class File
{
public:
  // A missing file is an empty one. Returns false only if it cannot be read.
  bool load(const QString& path, QString* error = nullptr);
  QString path() const { return m_path; }

  bool contains(const QString& section, const QString& key) const;
  QString value(const QString& section, const QString& key) const;
  void setValue(const QString& section, const QString& key, const QString& value);
  void remove(const QString& section, const QString& key);

  // Every (section, key) in the file, in file order.
  QList<QPair<QString, QString>> entries() const;

  bool isModified() const { return m_modified; }
  // Atomic: the old file stays intact unless the new one is fully written.
  bool save(QString* error = nullptr);

  QString text() const { return m_lines.join(QLatin1Char('\n')) + QLatin1Char('\n'); }

private:
  void reindex();
  QString m_path;
  QStringList m_lines;
  // "section/key" -> line index of the last definition (OpenMW's last wins).
  QMap<QString, int> m_index;
  QMap<QString, int> m_sectionEnd;
  bool m_modified{false};
};

}  // namespace OpenMWSettings

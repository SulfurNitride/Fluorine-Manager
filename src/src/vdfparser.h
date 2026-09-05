#ifndef VDFPARSER_H
#define VDFPARSER_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <memory>

/// A VDF value — either a string or a nested object (QMap).
class VdfValue
{
public:
  enum Type { String, Object };

  VdfValue() : m_type(String) {}
  explicit VdfValue(const QString& s) : m_type(String), m_string(s) {}
  explicit VdfValue(QMap<QString, VdfValue>&& obj)
      : m_type(Object), m_object(std::move(obj)) {}

  Type type() const { return m_type; }

  bool isString() const { return m_type == String; }
  bool isObject() const { return m_type == Object; }

  const QString& asString() const { return m_string; }
  const QMap<QString, VdfValue>& asObject() const { return m_object; }

  /// Get a nested value by key (returns nullptr if not an object or key missing).
  const VdfValue* get(const QString& key) const
  {
    if (m_type != Object) return nullptr;
    auto it = m_object.find(key);
    return (it != m_object.end()) ? &it.value() : nullptr;
  }

  /// Get a string value by key.
  QString getString(const QString& key) const
  {
    if (const auto* v = get(key); v && v->isString())
      return v->asString();
    return {};
  }

private:
  Type m_type;
  QString m_string;
  QMap<QString, VdfValue> m_object;
};

/// Parsed appmanifest_*.acf file.
struct AppManifestDepot {
  QString depot_id;
  QString manifest_id;
  qint64 size = 0;
  QString dlc_app_id;
};

struct AppManifest {
  QString app_id;
  QString name;
  QString install_dir;
  QString build_id;
  QString language;
  uint32_t state_flags = 0;
  QVector<AppManifestDepot> installed_depots;

  bool isInstalled() const { return state_flags == 4; }

  __attribute__((visibility("default")))
  static AppManifest fromVdf(const QString& content);
};

/// Parse a VDF file content into a root VdfValue (Object).
VdfValue parseVdf(const QString& content);

/// Parse libraryfolders.vdf and return the list of library paths.
QStringList parseLibraryFolders(const QString& content);

#endif // VDFPARSER_H

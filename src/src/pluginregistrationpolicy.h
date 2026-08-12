#ifndef PLUGINREGISTRATIONPOLICY_H
#define PLUGINREGISTRATIONPOLICY_H

#include <QObject>
#include <QSet>
#include <QString>

#include <optional>

namespace PluginRegistration
{

enum class BatchDecision
{
  Attempt,
  DuplicateObject,
  FailedLogicalPlugin,
};

/**
 * Per-proxy-identifier admission state.
 *
 * Python exposes one logical object through multiple QObject interface
 * wrappers. A failed initialization rejects the remaining wrappers with the
 * same stable plugin name, while unrelated objects in the module remain
 * independently admissible. Duplicate QObject pointers are never processed
 * twice.
 */
class ProxiedBatchLedger
{
public:
  BatchDecision begin(QObject* object,
                      const std::optional<QString>& pluginName)
  {
    if (m_SeenObjects.contains(object)) {
      return BatchDecision::DuplicateObject;
    }
    m_SeenObjects.insert(object);

    if (pluginName && m_FailedLogicalPlugins.contains(*pluginName)) {
      return BatchDecision::FailedLogicalPlugin;
    }
    return BatchDecision::Attempt;
  }

  void reject(const std::optional<QString>& pluginName)
  {
    if (pluginName) {
      m_FailedLogicalPlugins.insert(*pluginName);
    }
  }

private:
  QSet<QObject*> m_SeenObjects;
  QSet<QString> m_FailedLogicalPlugins;
};

}  // namespace PluginRegistration

#endif  // PLUGINREGISTRATIONPOLICY_H

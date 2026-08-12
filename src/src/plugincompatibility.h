#ifndef PLUGINCOMPATIBILITY_H
#define PLUGINCOMPATIBILITY_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <set>
#include <utility>

namespace PluginCompatibility
{

struct Block
{
  QString id;
  QString reason;
};

std::optional<Block> blockedRule(const QString& gameName,
                                 const QStringList& pluginAncestry,
                                 const QSet<QString>& allowedRuleIds = {});

QSet<QString> environmentOverrides();

/**
 * Compatibility policy used before IPlugin::init().
 *
 * The managed IPluginGame is selected only after plugins have been loaded, but
 * an established instance already records its target game in Settings. Keep
 * that configured name as the pre-init authority and remember blocked plugin
 * names so a child can inherit its master's decision even though the blocked
 * master was deliberately never registered.
 */
class RegistrationPolicy
{
public:
  explicit RegistrationPolicy(QString configuredGameName = {},
                              QSet<QString> allowedRuleIds = {})
      : m_ConfiguredGameName(std::move(configuredGameName)),
        m_AllowedRuleIds(std::move(allowedRuleIds))
  {}

  std::optional<Block> block(const QString& pluginName,
                             const QString& masterName = {});

  /** Whether a descendant name could affect this startup's decision. */
  bool needsMasterMetadata() const;

  /** Whether another resolved game name selects the same pre-init rules. */
  bool matchesGame(const QString& gameName) const;

private:
  QString m_ConfiguredGameName;
  QSet<QString> m_AllowedRuleIds;
  QHash<QString, Block> m_BlockedPlugins;
};

/**
 * Retire a compatibility-rejected proxy identifier. The proxy callback runs
 * first while QObject child holders still keep Python objects alive; unique
 * wrappers are destroyed afterward. If unload throws, the wrappers remain
 * alive so proxies retaining non-owning handles cannot observe dangling state.
 */
void retireRejectedProxiedBatch(const QSet<QObject*>& objects,
                                const std::function<void()>& unload);

template <typename Plugin, typename NameGetter, typename MasterGetter>
std::optional<Block> blockedRuleForPlugin(const QString& gameName, Plugin* plugin,
                                          NameGetter nameGetter,
                                          MasterGetter masterGetter,
                                          const QSet<QString>& allowedRuleIds = {})
{
  QStringList ancestry;
  std::set<Plugin*> visited;
  while (plugin != nullptr && visited.insert(plugin).second) {
    ancestry.append(nameGetter(plugin));
    plugin = masterGetter(plugin);
  }
  return blockedRule(gameName, ancestry, allowedRuleIds);
}

}  // namespace PluginCompatibility

#endif  // PLUGINCOMPATIBILITY_H

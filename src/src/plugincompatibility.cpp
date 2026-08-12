#include "plugincompatibility.h"

#include <QByteArray>
#include <QPointer>
#include <Qt>

namespace PluginCompatibility
{

namespace
{

const QString openMWPlayerRuleId = QStringLiteral("openmwplayer-native-openmw");

}  // namespace

std::optional<Block> blockedRule(const QString& gameName,
                                 const QStringList& pluginAncestry,
                                 const QSet<QString>& allowedRuleIds)
{
  if (allowedRuleIds.contains(openMWPlayerRuleId)) {
    return std::nullopt;
  }
  if (gameName.compare(QStringLiteral("Morrowind (OpenMW)"),
                       Qt::CaseInsensitive) != 0) {
    return std::nullopt;
  }
  if (!pluginAncestry.contains(QStringLiteral("OpenMWPlayer"))) {
    return std::nullopt;
  }
  return Block{
      openMWPlayerRuleId,
      QStringLiteral("OpenMWPlayer conflicts with Fluorine's native OpenMW "
                     "configuration and launch management."),
  };
}

QSet<QString> environmentOverrides()
{
  QSet<QString> result;
  const auto value = qgetenv("FLUORINE_ALLOW_INCOMPATIBLE_PLUGINS");
  for (const auto& part : value.split(',')) {
    const auto id = QString::fromUtf8(part).trimmed();
    if (!id.isEmpty()) {
      result.insert(id);
    }
  }
  return result;
}

std::optional<Block> RegistrationPolicy::block(const QString& pluginName,
                                               const QString& masterName)
{
  if (const auto existing = m_BlockedPlugins.constFind(pluginName);
      existing != m_BlockedPlugins.cend()) {
    return *existing;
  }

  std::optional<Block> result;
  if (!masterName.isEmpty()) {
    if (const auto master = m_BlockedPlugins.constFind(masterName);
        master != m_BlockedPlugins.cend()) {
      result = *master;
    }
  }
  if (!result) {
    QStringList ancestry{pluginName};
    if (!masterName.isEmpty()) {
      ancestry.append(masterName);
    }
    result = blockedRule(m_ConfiguredGameName, ancestry, m_AllowedRuleIds);
  }

  if (result) {
    m_BlockedPlugins.insert(pluginName, *result);
  }
  return result;
}

bool RegistrationPolicy::needsMasterMetadata() const
{
  return blockedRule(m_ConfiguredGameName,
                     {QStringLiteral("OpenMWPlayer")},
                     m_AllowedRuleIds)
      .has_value();
}

bool RegistrationPolicy::matchesGame(const QString& gameName) const
{
  RegistrationPolicy other(gameName, m_AllowedRuleIds);
  return needsMasterMetadata() == other.needsMasterMetadata();
}

void retireRejectedProxiedBatch(const QSet<QObject*>& objects,
                                const std::function<void()>& unload)
{
  QList<QPointer<QObject>> liveObjects;
  liveObjects.reserve(objects.size());
  for (QObject* object : objects) {
    liveObjects.append(object);
  }

  // Do not retire the QObject holders if proxy teardown fails. Some proxies
  // retain non-owning handles whose lifetime is supplied by these objects; a
  // best-effort delete after a failed unload would turn the original exception
  // into a latent use-after-free. Leaking this rejected exceptional batch until
  // process exit is the fail-safe outcome.
  unload();

  for (const auto& object : liveObjects) {
    if (!object.isNull()) {
      delete object;
    }
  }
}

}  // namespace PluginCompatibility

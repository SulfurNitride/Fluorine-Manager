#include "plugincontainer.h"
#include "iuserinterface.h"
#include "organizercore.h"
#include "organizerproxy.h"
#include "plugincompatibility.h"
#include "pluginregistrationpolicy.h"
#include "pluginregistrationtransaction.h"
#include "report.h"
#include "shared/appconfig.h"
#include <QAction>
#include <QCoreApplication>
#include <QDirIterator>
#include <QHash>
#include <QMessageBox>
#include <QSet>
#include <QToolButton>
#include <QSettings>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/fusion/include/at_key.hpp>
#include <boost/fusion/include/for_each.hpp>
#include <boost/fusion/sequence/intrinsic/at_key.hpp>
#include <idownloadmanager.h>
#include <ipluginproxy.h>

using namespace MOBase;
using namespace MOShared;

namespace bf = boost::fusion;

static void printPluginDiagToStderr(const QString&)
{
}

constexpr auto CompatibilityBlockedProperty =
    "fluorineCompatibilityBlocked";

static std::optional<PluginCompatibility::Block>
compatibilityBlock(const PluginContainer& container, IPlugin* plugin)
{
  if (plugin == nullptr || container.managedGame() == nullptr) {
    return std::nullopt;
  }

  const auto overrides = PluginCompatibility::environmentOverrides();

  // A pre-init rejected master is deliberately absent from the container. A
  // descendant whose master metadata became available only after init() must
  // still be disabled by the runtime fallback using its declared master name.
  try {
    if (const auto direct = PluginCompatibility::blockedRule(
            container.managedGame()->gameName(),
            {plugin->name(), plugin->master()}, overrides)) {
      return direct;
    }
  } catch (...) {
    // The ordinary registered ancestry below remains the fallback for plugins
    // whose optional identity metadata is still unavailable.
  }

  return PluginCompatibility::blockedRuleForPlugin(
      container.managedGame()->gameName(), plugin,
      [](IPlugin* current) { return current->name(); },
      [&container](IPlugin* current) {
        return container.requirements(current).master();
      },
      overrides);
}

// Welcome to the wonderful world of MO2 plugin management!
//
// We'll start by the C++ side.
//
// There are 9 types of MO2 plugins, two of which cannot be standalone: IPluginDiagnose
// and IPluginFileMapper. This means that you can have a class implementing IPluginGame,
// IPluginDiagnose and IPluginFileMapper. It is not possible for a class to implement
// two full plugin types (e.g. IPluginPreview and IPluginTool).
//
// Plugins are fetch as QObject initially and must be "qobject-casted" to the right
// type.
//
// Plugins are stored in the PluginContainer class in various C++ containers: there is a
// vector that stores all the plugin as QObject, multiple vectors that stores the plugin
// of each types, a map to find IPlugin object from their names or from IPluginDiagnose
// or IFileMapper (since these do not inherit IPlugin, they cannot be downcasted).
//
// Requirements for plugins are stored in m_Requirements:
// - IPluginGame cannot be enabled by user. A game plugin is considered enable only if
// it is
//   the one corresponding to the currently managed games.
// - If a plugin has a master plugin (IPlugin::master()), it cannot be enabled/disabled
// by users,
//   and will follow the enabled/disabled state of its parent.
// - Each plugin has an "enabled" setting stored in persistence.  If the setting does
// not exist,
//   the plugin's enabledByDefault is used instead.
// - A plugin is considered disabled if the setting is false.
// - If the setting is true, a plugin is considered disabled if one of its
//   requirements is not met.
// - Users cannot enable a plugin if one of its requirements is not met.
//
// Now let's move to the Proxy side... Or the as of now, the Python side.
//
// Proxied plugins are much more annoying because they can implement all interfaces, and
// are given to MO2 as separate plugins... A Python class implementing IPluginGame and
// IPluginDiagnose will be seen by MO2 as two separate QObject, and they will all have
// the same name.
//
// When a proxied plugin is registered, a few things must be taken care of:
// - There can only be one plugin mapped to a name in the PluginContainer class, so we
// keep the
//   plugin corresponding to the most relevant class (see PluginTypeOrder), e.g. if the
//   class inherits both IPluginGame and IPluginFileMapper, we map the name to the C++
//   QObject corresponding to the IPluginGame.
// - When a proxied plugin implements multiple interfaces, the IPlugin corresponding to
// the most
//   important interface is set as the parent (hidden) of the other IPlugin through
//   PluginRequirements. This way, the plugin are managed together (enabled/disabled
//   state). The "fake" children plugins will not be returned by
//   PluginRequirements::children().
// - Since each interface corresponds to a different QObject, we need to take care not
// to call
//   IPlugin::init() on each QObject, but only on the first one.
//
// All the proxied plugins are linked to the proxy plugin by PluginRequirements. If the
// proxy plugin is disabled, the proxied plugins are not even loaded so not visible in
// the plugin management tab.

template <class T>
struct PluginTypeName;

template <>
struct PluginTypeName<MOBase::IPlugin>
{
  static QString value() { return PluginContainer::tr("Plugin"); }
};
template <>
struct PluginTypeName<MOBase::IPluginDiagnose>
{
  static QString value() { return PluginContainer::tr("Diagnose"); }
};
template <>
struct PluginTypeName<MOBase::IPluginGame>
{
  static QString value() { return PluginContainer::tr("Game"); }
};
template <>
struct PluginTypeName<MOBase::IPluginInstaller>
{
  static QString value() { return PluginContainer::tr("Installer"); }
};
template <>
struct PluginTypeName<MOBase::IPluginModPage>
{
  static QString value() { return PluginContainer::tr("Mod Page"); }
};
template <>
struct PluginTypeName<MOBase::IPluginPreview>
{
  static QString value() { return PluginContainer::tr("Preview"); }
};
template <>
struct PluginTypeName<MOBase::IPluginTool>
{
  static QString value() { return PluginContainer::tr("Tool"); }
};
template <>
struct PluginTypeName<MOBase::IPluginProxy>
{
  static QString value() { return PluginContainer::tr("Proxy"); }
};
template <>
struct PluginTypeName<MOBase::IPluginFileMapper>
{
  static QString value() { return PluginContainer::tr("File Mapper"); }
};

QStringList PluginContainer::pluginInterfaces()
{
  // Find all the names:
  QStringList names;
  boost::mp11::mp_for_each<PluginTypeOrder>([&names](const auto* p) {
    using plugin_type = std::decay_t<decltype(*p)>;
    auto name         = PluginTypeName<plugin_type>::value();
    if (!name.isEmpty()) {
      names.append(name);
    }
  });

  return names;
}

// PluginRequirementProxy

const std::set<QString> PluginRequirements::s_CorePlugins{"INI Bakery"};

PluginRequirements::PluginRequirements(PluginContainer* pluginContainer,
                                       MOBase::IPlugin* plugin, OrganizerProxy* proxy,
                                       MOBase::IPluginProxy* pluginProxy)
    : m_PluginContainer(pluginContainer), m_Plugin(plugin), m_PluginProxy(pluginProxy),
       m_Organizer(proxy)
{
  // There are a lots of things we cannot set here (e.g. m_Master) because we do not
  // know the order plugins are loaded.
}

void PluginRequirements::fetchRequirements()
{
  m_Requirements = m_Plugin->requirements();
}

IPluginProxy* PluginRequirements::proxy() const
{
  return m_PluginProxy;
}

std::vector<IPlugin*> PluginRequirements::proxied() const
{
  std::vector<IPlugin*> children;
  if (dynamic_cast<IPluginProxy*>(m_Plugin)) {
    for (auto* obj : m_PluginContainer->plugins<QObject>()) {
      auto* plugin = qobject_cast<IPlugin*>(obj);
      if (plugin && m_PluginContainer->requirements(plugin).proxy() == m_Plugin) {
        children.push_back(plugin);
      }
    }
  }
  return children;
}

IPlugin* PluginRequirements::master() const
{
  // If we have a m_Master, it was forced and thus override the default master().
  if (m_Master) {
    return m_Master;
  }

  if (m_Plugin->master().isEmpty()) {
    return nullptr;
  }

  return m_PluginContainer->plugin(m_Plugin->master());
}

void PluginRequirements::setMaster(IPlugin* master)
{
  m_Master = master;
}

std::vector<IPlugin*> PluginRequirements::children() const
{
  std::vector<IPlugin*> children;
  for (auto* obj : m_PluginContainer->plugins<QObject>()) {
    auto* plugin = qobject_cast<IPlugin*>(obj);

    // Not checking master() but requirements().master() due to "hidden"
    // masters.
    // If the master has the same name as the plugin, this is a "hidden"
    // master, we do not add it here.
    if (plugin && m_PluginContainer->requirements(plugin).master() == m_Plugin &&
        plugin->name() != m_Plugin->name()) {
      children.push_back(plugin);
    }
  }
  return children;
}

std::vector<IPluginRequirement::Problem> PluginRequirements::problems() const
{
  std::vector<IPluginRequirement::Problem> result;
  for (const auto& requirement : m_Requirements) {
    if (auto p = requirement->check(m_Organizer)) {
      result.push_back(*p);
    }
  }
  return result;
}

bool PluginRequirements::canEnable() const
{
  return problems().empty();
}

bool PluginRequirements::isCorePlugin() const
{
  // Let's consider game plugins as "core":
  if (m_PluginContainer->implementInterface<IPluginGame>(m_Plugin)) {
    return true;
  }

  return s_CorePlugins.contains(m_Plugin->name());
}

bool PluginRequirements::hasRequirements() const
{
  return !m_Requirements.empty();
}

QStringList PluginRequirements::requiredGames() const
{
  // We look for a "GameDependencyRequirement" - There can be only one since otherwise
  // it'd mean that the plugin requires two games at once.
  for (const auto& requirement : m_Requirements) {
    if (const auto* gdep =
            dynamic_cast<const GameDependencyRequirement*>(requirement.get())) {
      return gdep->gameNames();
    }
  }

  return {};
}

std::vector<MOBase::IPlugin*> PluginRequirements::requiredFor() const
{
  std::vector<MOBase::IPlugin*> required;
  std::set<MOBase::IPlugin*> visited;
  requiredFor(required, visited);
  return required;
}

void PluginRequirements::requiredFor(std::vector<MOBase::IPlugin*>& required,
                                     std::set<MOBase::IPlugin*>& visited) const
{
  // Handle cyclic dependencies.
  if (visited.contains(m_Plugin)) {
    return;
  }
  visited.insert(m_Plugin);

  for (auto& [plugin, requirements] : m_PluginContainer->m_Requirements) {

    // If the plugin is not enabled, discard:
    if (!m_PluginContainer->isEnabled(plugin)) {
      continue;
    }

    // Check the requirements:
    for (auto& requirement : requirements.m_Requirements) {

      // We check for plugin dependency. Game dependency are not checked this way.
      if (const auto* pdep =
              dynamic_cast<const PluginDependencyRequirement*>(requirement.get())) {

        // Check if at least one of the plugin in the requirements is enabled (except
        // this one):
        bool oneEnabled = false;
        for (auto& pluginName : pdep->pluginNames()) {
          if (pluginName != m_Plugin->name() &&
              m_PluginContainer->isEnabled(pluginName)) {
            oneEnabled = true;
            break;
          }
        }

        // No plugin enabled found, so the plugin requires this plugin:
        if (!oneEnabled) {
          required.push_back(plugin);
          requirements.requiredFor(required, visited);
          break;
        }
      }
    }
  }
}

// PluginContainer

PluginContainer::PluginContainer(OrganizerCore* organizer,
                                 QString configuredGameName)
    : m_Organizer(organizer),
      m_GameFeatures(std::make_unique<GameFeatures>(organizer, this)),
      m_PreviewGenerator(*this),
      m_CompatibilityRegistration(
          std::move(configuredGameName),
          PluginCompatibility::environmentOverrides())
{}

PluginContainer::~PluginContainer()
{
  try {
    unloadPlugins();
  } catch (const std::exception& e) {
    log::error("failed to unload plugins during shutdown: {}", e.what());
  } catch (...) {
    log::error("failed to unload plugins during shutdown: unknown exception");
  }
  m_Organizer = nullptr;
}

void PluginContainer::startPlugins(IUserInterface* userInterface)
{
  m_UserInterface = userInterface;
  startPluginsImpl(plugins<QObject>());
}

QStringList PluginContainer::implementedInterfaces(IPlugin* plugin) const
{
  // We need a QObject to be able to qobject_cast<> to the plugin types:
  QObject* oPlugin = as_qobject(plugin);

  if (!oPlugin) {
    return {};
  }

  return implementedInterfaces(oPlugin);
}

QStringList PluginContainer::implementedInterfaces(QObject* oPlugin)
{
  // Find all the names:
  QStringList names;
  boost::mp11::mp_for_each<PluginTypeOrder>([oPlugin, &names](const auto* p) {
    using plugin_type = std::decay_t<decltype(*p)>;
    if (qobject_cast<plugin_type*>(oPlugin)) {
      auto name = PluginTypeName<plugin_type>::value();
      if (!name.isEmpty()) {
        names.append(name);
      }
    }
  });

  // If the plugin implements at least one interface other than IPlugin, remove IPlugin:
  if (names.size() > 1) {
    names.removeAll(PluginTypeName<IPlugin>::value());
  }

  return names;
}

QString PluginContainer::topImplementedInterface(IPlugin* plugin) const
{
  auto interfaces = implementedInterfaces(plugin);
  return interfaces.isEmpty() ? "" : interfaces[0];
}

bool PluginContainer::isBetterInterface(QObject* lhs, QObject* rhs)
{
  int count = 0;
  int lhsIdx = -1;
  int rhsIdx = -1;
  boost::mp11::mp_for_each<PluginTypeOrder>([&](const auto* p) {
    using plugin_type = std::decay_t<decltype(*p)>;
    if (lhsIdx < 0 && qobject_cast<plugin_type*>(lhs)) {
      lhsIdx = count;
    }
    if (rhsIdx < 0 && qobject_cast<plugin_type*>(rhs)) {
      rhsIdx = count;
    }
    ++count;
  });
  return lhsIdx < rhsIdx;
}

QStringList PluginContainer::mergedProxyList(IPluginProxy* proxy) const
{
  const QString bundled =
      m_BundledPluginPath.isEmpty() ? AppConfig::pluginsPath() : m_BundledPluginPath;
  const QString instance =
      m_PluginPath.isEmpty() ? AppConfig::pluginsPath() : m_PluginPath;

  QMap<QString, QString> merged;
  // Instance plugins first (lower priority)
  if (instance != bundled) {
    for (const auto& p : proxy->pluginList(instance))
      merged[QFileInfo(p).fileName()] = p;
  }
  // Bundled plugins overwrite (higher priority)
  for (const auto& p : proxy->pluginList(bundled))
    merged[QFileInfo(p).fileName()] = p;

  return merged.values();
}

QStringList PluginContainer::pluginFileNames() const
{
  QStringList result;
  for (QPluginLoader* loader : m_PluginLoaders) {
    result.append(loader->fileName());
  }
  std::vector<IPluginProxy*> proxyList = bf::at_key<IPluginProxy>(m_Plugins);
  for (IPluginProxy* proxy : proxyList) {
    result.append(mergedProxyList(proxy));
  }
  return result;
}

QObject* PluginContainer::as_qobject(MOBase::IPlugin* plugin) const
{
  // Find the correspond QObject - Can this be done safely with a cast?
  const auto& objects = bf::at_key<QObject>(m_Plugins);
  auto it =
      std::find_if(std::begin(objects), std::end(objects), [plugin](QObject* obj) {
        return qobject_cast<IPlugin*>(obj) == plugin;
      });

  if (it == std::end(objects)) {
    return nullptr;
  }

  return *it;
}

bool PluginContainer::initPlugin(IPlugin* plugin, bool skipInit)
{
  // when MO has no instance loaded, init() is not called on plugins, except
  // for proxy plugins, where init() is called with a null IOrganizer
  //
  // after proxies are initialized, instantiate() is called for all the plugins
  // they've discovered, but as for regular plugins, init() won't be
  // called on them if m_OrganizerCore is null

  if (plugin == nullptr) {
    return false;
  }

  // Check if it is a proxy plugin:
  bool isProxy = dynamic_cast<IPluginProxy*>(plugin);

  if (!m_Organizer && !isProxy) {
    return true;
  }

  if (skipInit) {
    return true;
  }

  auto it = m_Requirements.find(plugin);
  if (it == m_Requirements.end()) {
    return false;
  }

  OrganizerProxy* proxy = it->second.m_Organizer;
  bool initialized = false;
  if (proxy) {
    const bool admitted = proxy->runPluginCallIfAllowed([&] {
      initialized = plugin->init(proxy);
      if (initialized) {
        it->second.fetchRequirements();
      }
    });
    initialized = admitted && initialized;
  } else {
    initialized = plugin->init(proxy);
    if (initialized) {
      it->second.fetchRequirements();
    }
  }
  return initialized;
}

void PluginContainer::registerGame(IPluginGame* game)
{
  m_SupportedGames.insert({game->gameName(), game});
}

void PluginContainer::unregisterGame(MOBase::IPluginGame* game)
{
  m_SupportedGames.erase(game->gameName());
}

PluginContainer::PreInitCompatibilityDecision
PluginContainer::preInitCompatibility(IPlugin* plugin,
                                      const QString& filepath)
{
  PreInitCompatibilityDecision decision;
  try {
    decision.pluginName    = plugin->name();
    decision.nameAvailable = true;
  } catch (const std::exception& e) {
    log::warn("plugin name failed before initialization for '{}': {}",
              QDir::toNativeSeparators(filepath), e.what());
    return decision;
  } catch (...) {
    log::warn("plugin name failed before initialization for '{}': "
              "unknown exception",
              QDir::toNativeSeparators(filepath));
    return decision;
  }

  decision.block = m_CompatibilityRegistration.block(decision.pluginName);
  if (!decision.block &&
      m_CompatibilityRegistration.needsMasterMetadata()) {
    try {
      decision.block = m_CompatibilityRegistration.block(
          decision.pluginName, plugin->master());
    } catch (const std::exception& e) {
      log::debug("plugin '{}' did not expose pre-init master metadata: {}",
                 decision.pluginName, e.what());
    } catch (...) {
      log::debug("plugin '{}' did not expose pre-init master metadata",
                 decision.pluginName);
    }
  }
  return decision;
}

void PluginContainer::reportCompatibilityBlock(
    QObject* object, const QString& pluginName,
    const PluginCompatibility::Block& block)
{
  object->setProperty(CompatibilityBlockedProperty, true);
  if (!m_ReportedCompatibilityBlocks.contains(block.id)) {
    m_ReportedCompatibilityBlocks.insert(block.id);
    log::warn(
        "compatibility rule '{}' prevented plugin '{}' from initializing: {} "
        "Set FLUORINE_ALLOW_INCOMPATIBLE_PLUGINS={} to override.",
        block.id, pluginName, block.reason, block.id);
  } else {
    log::debug("compatibility rule '{}' also rejected plugin '{}'",
               block.id, pluginName);
  }
}

IPlugin* PluginContainer::registerPlugin(
    QObject* plugin, const QString& filepath,
    MOBase::IPluginProxy* pluginProxy,
    std::optional<QString> compatibilityName)
{
  // generic treatment for all plugins
  IPlugin* pluginObj = qobject_cast<IPlugin*>(plugin);
  if (pluginObj == nullptr) {
    log::debug("PluginContainer::registerPlugin() called with a non IPlugin QObject.");
    return nullptr;
  }

  // The managed-game pointer is selected only after plugin discovery, so the
  // existing isEnabled()/startPlugins() checks are too late to prevent init()
  // side effects. Use the instance's configured game name and, only when the
  // rule is active, the plugin's declared master before publishing this object
  // anywhere in the container. Master metadata that is unavailable before
  // init() remains fail-open and is covered by the later runtime check.
  QString pluginName;
  if (compatibilityName) {
    pluginName = *compatibilityName;
  } else {
    const auto decision = preInitCompatibility(pluginObj, filepath);
    if (!decision.nameAvailable) {
      return nullptr;
    }
    pluginName = decision.pluginName;
    if (decision.block) {
      reportCompatibilityBlock(plugin, pluginName, *decision.block);
      return nullptr;
    }
  }

  const QStringList interfaces = implementedInterfaces(plugin);

  enum class PrimaryInterface
  {
    ModPage,
    Game,
    Tool,
    Installer,
    Preview,
    Proxy,
    Base,
  };
  PrimaryInterface primary = PrimaryInterface::Base;
  if (qobject_cast<IPluginModPage*>(plugin)) {
    primary = PrimaryInterface::ModPage;
  } else if (qobject_cast<IPluginGame*>(plugin)) {
    primary = PrimaryInterface::Game;
  } else if (qobject_cast<IPluginTool*>(plugin)) {
    primary = PrimaryInterface::Tool;
  } else if (qobject_cast<IPluginInstaller*>(plugin)) {
    primary = PrimaryInterface::Installer;
  } else if (qobject_cast<IPluginPreview*>(plugin)) {
    primary = PrimaryInterface::Preview;
  } else if (qobject_cast<IPluginProxy*>(plugin)) {
    primary = PrimaryInterface::Proxy;
  }

  // If we already have a plugin with this name, another wrapper from the same
  // proxy identifier represents the same logical Python object. It shares the
  // first wrapper's initialization result; unrelated duplicates are rejected.
  bool skipInit = false;
  auto& mapNames = bf::at_key<QString>(m_AccessPlugins);
  const auto previousNameEntry = mapNames.find(pluginName);
  const bool hadPreviousName   = previousNameEntry != mapNames.end();
  IPlugin* previousPlugin = hadPreviousName ? previousNameEntry->second : nullptr;
  if (hadPreviousName) {

    // If both plugins are from the same proxy and the same file, this is usually
    // ok (in theory some one could write two different classes from the same Python
    // file/module):
    if (pluginProxy && m_Requirements.at(previousPlugin).proxy() == pluginProxy &&
        this->filepath(previousPlugin) == QDir::cleanPath(filepath)) {

      // Plugin has already been initialized:
      skipInit = true;
    } else {
      log::warn("Trying to register two plugins with the name '{}' (from {} and {}), "
                "the second one will not be registered.",
                pluginName, this->filepath(previousPlugin),
                QDir::cleanPath(filepath));
      return nullptr;
    }
  }

  // Existing plugins are allowed to read their registered settings and query
  // their own container identity from init(). Stage only those prerequisites,
  // then roll every host-owned mutation back unless initialization succeeds.
  auto& objects = bf::at_key<QObject>(m_Plugins);
  QObject* previousParent = plugin->parent();
  const QVariant previousFilepath = plugin->property("filepath");
  OrganizerProxy* organizerProxy = nullptr;
  bool requirementsInserted = false;

  const auto erasePlugin = [](auto& plugins, auto* value) noexcept {
    const auto found = std::find(plugins.rbegin(), plugins.rend(), value);
    if (found != plugins.rend()) {
      plugins.erase(std::prev(found.base()));
    }
  };
  PluginRegistration::Transaction registration;

  try {
    if (!hadPreviousName ||
        isBetterInterface(plugin, as_qobject(previousPlugin))) {
      if (hadPreviousName) {
        log::debug(
            "replacing plugin '{}' with interfaces [{}] by one with interfaces [{}]",
            pluginName, implementedInterfaces(previousPlugin).join(", "),
            interfaces.join(", "));
      }
      registration.stage(
          [&] { mapNames[pluginName] = pluginObj; },
          [&] {
            if (hadPreviousName) {
              mapNames[pluginName] = previousPlugin;
            } else {
              mapNames.erase(pluginName);
            }
          });
    }

    // Storing the original QObject* is a bit of a hack as I couldn't figure out
    // any way to cast directly between IPlugin* and IPluginDiagnose*.
    registration.stage(
        [&] {
          objects.push_back(plugin);
          plugin->setProperty("filepath", QDir::cleanPath(filepath));
          plugin->setParent(this);
        },
        [&] {
          erasePlugin(objects, plugin);
          plugin->setProperty("filepath", previousFilepath);
          plugin->setParent(previousParent);
        });

    // Same-name proxy wrappers represent one logical plugin and reuse the
    // settings staged by the first successfully initialized wrapper.
    if (m_Organizer) {
      registration.stage(
          [&] {
            if (skipInit) {
              m_Organizer->settings().plugins().registerPluginInterface(pluginObj);
            } else {
              m_Organizer->settings().plugins().stagePluginRegistration(
                  pluginObj, pluginName);
            }
          },
          [&] {
            if (skipInit) {
              m_Organizer->settings().plugins().unregisterPluginInterface(pluginObj);
            } else {
              m_Organizer->settings().plugins().unregisterPlugin(pluginObj,
                                                                 pluginName);
            }
          });
    }

    registration.stage(
        [&] {
          if (m_Organizer) {
            QString instancePluginDirectory;
            const QString canonicalInstance = QDir(m_PluginPath).canonicalPath();
            const QString canonicalBundled =
                QDir(m_BundledPluginPath).canonicalPath();
            if (!canonicalInstance.isEmpty() && !canonicalBundled.isEmpty() &&
                canonicalInstance != canonicalBundled) {
              instancePluginDirectory = m_PluginPath;
            }
            organizerProxy = new OrganizerProxy(
                m_Organizer, this, pluginObj, filepath, instancePluginDirectory);
            organizerProxy->setParent(plugin);
          }
          const auto [requirement, inserted] = m_Requirements.emplace(
              pluginObj,
              PluginRequirements(this, pluginObj, organizerProxy, pluginProxy));
          requirementsInserted = inserted;
          if (!inserted) {
            throw std::runtime_error("duplicate plugin requirements entry");
          }
        },
        [&] {
          if (requirementsInserted) {
            m_Requirements.erase(pluginObj);
          }
          delete organizerProxy;
          organizerProxy = nullptr;
        });

    if (primary == PrimaryInterface::Game) {
      qobject_cast<IPluginGame*>(plugin)->detectGame();
    }

    if (!registration.initializeOnce(
            [&] { return initPlugin(pluginObj, skipInit); })) {
      log::warn("plugin '{}' from '{}' failed its single initialization attempt; "
                "interfaces [{}]",
                pluginName, QDir::toNativeSeparators(filepath),
                interfaces.join(", "));
      return nullptr;
    }

    // Initialization succeeded exactly once. Publish auxiliary and primary
    // interfaces while rollback remains armed; external signals follow commit.
    if (auto* diagnose = qobject_cast<IPluginDiagnose*>(plugin)) {
      registration.stage(
          [&, diagnose] {
            bf::at_key<IPluginDiagnose>(m_Plugins).push_back(diagnose);
            bf::at_key<IPluginDiagnose>(m_AccessPlugins)[diagnose] = pluginObj;
            diagnose->onInvalidated([this]() { emit diagnosisUpdate(); });
          },
          [&, diagnose] {
            bf::at_key<IPluginDiagnose>(m_AccessPlugins).erase(diagnose);
            erasePlugin(bf::at_key<IPluginDiagnose>(m_Plugins), diagnose);
            // Host state must already be clean if untrusted callback teardown
            // throws. Transaction will contain the exception and continue with
            // the remaining rollback stages.
            diagnose->onInvalidated({});
          });
    }
    if (auto* mapper = qobject_cast<IPluginFileMapper*>(plugin)) {
      registration.stage(
          [&, mapper] {
            bf::at_key<IPluginFileMapper>(m_Plugins).push_back(mapper);
            bf::at_key<IPluginFileMapper>(m_AccessPlugins)[mapper] = pluginObj;
          },
          [&, mapper] {
            bf::at_key<IPluginFileMapper>(m_AccessPlugins).erase(mapper);
            erasePlugin(bf::at_key<IPluginFileMapper>(m_Plugins), mapper);
          });
    }

    registration.stage(
        [&] {
          switch (primary) {
          case PrimaryInterface::ModPage:
            bf::at_key<IPluginModPage>(m_Plugins).push_back(
                qobject_cast<IPluginModPage*>(plugin));
            break;
          case PrimaryInterface::Game: {
            auto* game = qobject_cast<IPluginGame*>(plugin);
            bf::at_key<IPluginGame>(m_Plugins).push_back(game);
            registerGame(game);
            break;
          }
          case PrimaryInterface::Tool:
            bf::at_key<IPluginTool>(m_Plugins).push_back(
                qobject_cast<IPluginTool*>(plugin));
            break;
          case PrimaryInterface::Installer: {
            auto* installer = qobject_cast<IPluginInstaller*>(plugin);
            bf::at_key<IPluginInstaller>(m_Plugins).push_back(installer);
            if (m_Organizer) {
              installer->setInstallationManager(
                  m_Organizer->installationManager());
            }
            break;
          }
          case PrimaryInterface::Preview:
            bf::at_key<IPluginPreview>(m_Plugins).push_back(
                qobject_cast<IPluginPreview*>(plugin));
            break;
          case PrimaryInterface::Proxy:
            bf::at_key<IPluginProxy>(m_Plugins).push_back(
                qobject_cast<IPluginProxy*>(plugin));
            break;
          case PrimaryInterface::Base:
            bf::at_key<IPlugin>(m_Plugins).push_back(pluginObj);
            break;
          }
        },
        [&] {
          switch (primary) {
          case PrimaryInterface::ModPage:
            erasePlugin(bf::at_key<IPluginModPage>(m_Plugins),
                        qobject_cast<IPluginModPage*>(plugin));
            break;
          case PrimaryInterface::Game:
            for (auto game = m_SupportedGames.begin();
                 game != m_SupportedGames.end();) {
              if (game->second == qobject_cast<IPluginGame*>(plugin)) {
                game = m_SupportedGames.erase(game);
              } else {
                ++game;
              }
            }
            erasePlugin(bf::at_key<IPluginGame>(m_Plugins),
                        qobject_cast<IPluginGame*>(plugin));
            break;
          case PrimaryInterface::Tool:
            erasePlugin(bf::at_key<IPluginTool>(m_Plugins),
                        qobject_cast<IPluginTool*>(plugin));
            break;
          case PrimaryInterface::Installer:
            erasePlugin(bf::at_key<IPluginInstaller>(m_Plugins),
                        qobject_cast<IPluginInstaller*>(plugin));
            break;
          case PrimaryInterface::Preview:
            erasePlugin(bf::at_key<IPluginPreview>(m_Plugins),
                        qobject_cast<IPluginPreview*>(plugin));
            break;
          case PrimaryInterface::Proxy:
            erasePlugin(bf::at_key<IPluginProxy>(m_Plugins),
                        qobject_cast<IPluginProxy*>(plugin));
            break;
          case PrimaryInterface::Base:
            erasePlugin(bf::at_key<IPlugin>(m_Plugins), pluginObj);
            break;
          }
        });
  } catch (const std::exception& e) {
    log::warn("plugin '{}' from '{}' threw during initialization; interfaces [{}]: {}",
              pluginName, QDir::toNativeSeparators(filepath),
              interfaces.join(", "), e.what());
    return nullptr;
  } catch (...) {
    log::warn("plugin '{}' from '{}' threw during initialization; interfaces [{}]: "
              "unknown exception",
              pluginName, QDir::toNativeSeparators(filepath),
              interfaces.join(", "));
    return nullptr;
  }

  registration.commit();

  // Durable legacy-settings migration is post-commit housekeeping. A plugin
  // that rejects initialization must not change persistent settings.
  if (m_Organizer && !skipInit) {
    try {
      m_Organizer->settings().plugins().commitPluginRegistration(pluginName);
    } catch (const std::exception& e) {
      log::warn("failed to migrate legacy settings for plugin '{}': {}",
                pluginName, e.what());
    } catch (...) {
      log::warn("failed to migrate legacy settings for plugin '{}': "
                "unknown exception",
                pluginName);
    }
  }

  IPlugin* registered = pluginObj;
  if (primary != PrimaryInterface::Preview) {
    emit pluginRegistered(pluginObj);
  }
  if (primary == PrimaryInterface::Proxy) {
    auto* proxy = qobject_cast<IPluginProxy*>(plugin);
    try {
      const QStringList filepaths = mergedProxyList(proxy);
      log::debug("proxy '{}' discovered {} proxied plugin candidate(s)",
                 pluginName, filepaths.size());
      printPluginDiagToStderr(
          QString("proxy '%1' discovered %2 proxied plugin candidate(s)")
              .arg(pluginName)
              .arg(filepaths.size()));
      for (const QString& proxiedFilepath : filepaths) {
        log::debug("proxy '{}' candidate: '{}'", pluginName,
                   QDir::toNativeSeparators(proxiedFilepath));
        loadProxied(proxiedFilepath, proxy);
      }
    } catch (const std::exception& e) {
      log::error("proxy '{}' failed while discovering candidates: {}",
                 pluginName, e.what());
    } catch (...) {
      log::error("proxy '{}' failed while discovering candidates: "
                 "unknown exception",
                 pluginName);
    }
  }
  return registered;
}

IPluginGame* PluginContainer::managedGame() const
{
  // TODO: This const_cast is safe but ugly. Most methods require a IPlugin*, so
  // returning a const-version if painful. This should be fixed by making methods accept
  // a const IPlugin* instead, but there are a few tricks with qobject_cast and const.
  return m_Organizer ? const_cast<IPluginGame*>(m_Organizer->managedGame()) : nullptr;
}

bool PluginContainer::isEnabled(IPlugin* plugin) const
{
  if (::compatibilityBlock(*this, plugin)) {
    return false;
  }

  // Check if it's a game plugin:
  if (implementInterface<IPluginGame>(plugin)) {
    return plugin == m_Organizer->managedGame();
  }

  // Check the master, if any:
  const auto& requirements = m_Requirements.at(plugin);

  if (requirements.master()) {
    return isEnabled(requirements.master());
  }

  // Check if the plugin is enabled:
  if (!m_Organizer->persistent(plugin->name(), "enabled", plugin->enabledByDefault())
           .toBool()) {
    return false;
  }

  // Check the requirements:
  return m_Requirements.at(plugin).canEnable();
}

void PluginContainer::setEnabled(MOBase::IPlugin* plugin, bool enable,
                                 bool dependencies)
{
  // If required, disable dependencies:
  if (!enable && dependencies) {
    for (auto* p : requirements(plugin).requiredFor()) {
      // No need to "recurse" here since requiredFor already does it.
      setEnabled(p, false, false);
    }
  }

  // Always disable/enable child plugins:
  for (auto* p : requirements(plugin).children()) {
    // "Child" plugin should have no dependencies.
    setEnabled(p, enable, false);
  }

  m_Organizer->setPersistent(plugin->name(), "enabled", enable, true);

  if (enable) {
    emit pluginEnabled(plugin);
  } else {
    emit pluginDisabled(plugin);
  }
}

void PluginContainer::notifyEnabledStateRestored(MOBase::IPlugin* plugin,
                                                 bool enabled)
{
  if (enabled) {
    emit pluginEnabled(plugin);
  } else {
    emit pluginDisabled(plugin);
  }
}

MOBase::IPlugin* PluginContainer::plugin(QString const& pluginName) const
{
  const auto& map = bf::at_key<QString>(m_AccessPlugins);
  auto it   = map.find(pluginName);
  if (it == std::end(map)) {
    return nullptr;
  }
  return it->second;
}

MOBase::IPlugin* PluginContainer::plugin(MOBase::IPluginDiagnose* diagnose) const
{
  const auto& map = bf::at_key<IPluginDiagnose>(m_AccessPlugins);
  auto it   = map.find(diagnose);
  if (it == std::end(map)) {
    return nullptr;
  }
  return it->second;
}

MOBase::IPlugin* PluginContainer::plugin(MOBase::IPluginFileMapper* mapper) const
{
  const auto& map = bf::at_key<IPluginFileMapper>(m_AccessPlugins);
  auto it   = map.find(mapper);
  if (it == std::end(map)) {
    return nullptr;
  }
  return it->second;
}

bool PluginContainer::isEnabled(QString const& pluginName) const
{
  IPlugin* p = plugin(pluginName);
  return p ? isEnabled(p) : false;
}
bool PluginContainer::isEnabled(MOBase::IPluginDiagnose* diagnose) const
{
  IPlugin* p = plugin(diagnose);
  return p ? isEnabled(p) : false;
}
bool PluginContainer::isEnabled(MOBase::IPluginFileMapper* mapper) const
{
  IPlugin* p = plugin(mapper);
  return p ? isEnabled(p) : false;
}

const PluginRequirements& PluginContainer::requirements(IPlugin* plugin) const
{
  return m_Requirements.at(plugin);
}

OrganizerProxy* PluginContainer::organizerProxy(MOBase::IPlugin* plugin) const
{
  return requirements(plugin).m_Organizer;
}

std::shared_ptr<PluginCallGate>
PluginContainer::pluginCallGate(MOBase::IPlugin* plugin) const
{
  if (plugin == nullptr) {
    return {};
  }
  const auto requirement = m_Requirements.find(plugin);
  if (requirement == m_Requirements.end() ||
      requirement->second.m_Organizer == nullptr) {
    return {};
  }
  return requirement->second.m_Organizer->mutationGate();
}

MOBase::IPluginProxy* PluginContainer::pluginProxy(MOBase::IPlugin* plugin) const
{
  return requirements(plugin).proxy();
}

QString PluginContainer::filepath(MOBase::IPlugin* plugin) const
{
  return as_qobject(plugin)->property("filepath").toString();
}

IPluginGame* PluginContainer::game(const QString& name) const
{
  auto iter = m_SupportedGames.find(name);
  if (iter != m_SupportedGames.end()) {
    return iter->second;
  } else {
    return nullptr;
  }
}

void PluginContainer::startPluginsImpl(const std::vector<QObject*>& plugins) const
{
  // setUserInterface()
  if (m_UserInterface) {
    for (auto* object : plugins) {
      auto* plugin = qobject_cast<IPlugin*>(object);
      if (!plugin) {
        continue;
      }
      organizerProxy(plugin)->runPluginCallIfAllowed([&] {
        if (::compatibilityBlock(*this, plugin)) {
          return;
        }
        if (auto* proxy = qobject_cast<IPluginProxy*>(object)) {
          proxy->setParentWidget(m_UserInterface->mainWindow());
        }
        if (auto* modPage = qobject_cast<IPluginModPage*>(object)) {
          modPage->setParentWidget(m_UserInterface->mainWindow());
        }
        if (auto* tool = qobject_cast<IPluginTool*>(object)) {
          tool->setParentWidget(m_UserInterface->mainWindow());
        }
        if (auto* installer = qobject_cast<IPluginInstaller*>(object)) {
          installer->setParentWidget(m_UserInterface->mainWindow());
        }
      });
    }
  }

  // Trigger initial callbacks, e.g. onUserInterfaceInitialized and onProfileChanged.
  if (m_Organizer) {
    for (auto* object : plugins) {
      auto* plugin = qobject_cast<IPlugin*>(object);
      auto* oproxy = organizerProxy(plugin);
      oproxy->runPluginCallIfAllowed([&] {
        if (const auto block = ::compatibilityBlock(*this, plugin)) {
          if (plugin->name() == QStringLiteral("OpenMWPlayer")) {
            log::warn(
                "compatibility rule '{}' disabled plugin '{}' for this session: {} "
                "Set FLUORINE_ALLOW_INCOMPATIBLE_PLUGINS={} to override.",
                block->id, plugin->name(), block->reason, block->id);
          }
          return;
        }
        oproxy->connectSignals();
        oproxy->m_ProfileChanged(nullptr, m_Organizer->currentProfile().get());

        if (m_UserInterface) {
          oproxy->m_UserInterfaceInitialized(m_UserInterface->mainWindow());
        }
      });
    }
  }
}

std::vector<QObject*> PluginContainer::loadProxied(const QString& filepath,
                                                   IPluginProxy* proxy)
{
  std::vector<QObject*> proxiedPlugins;
  QString proxyName = QStringLiteral("<null>");

  try {
    if (proxy) {
      proxyName = proxy->name();
    }
    log::debug("loading proxied plugin candidate '{}' via proxy '{}'",
              QDir::toNativeSeparators(filepath), proxyName);
    printPluginDiagToStderr(
        QString("loading proxied plugin candidate '%1' via proxy '%2'")
            .arg(QDir::toNativeSeparators(filepath))
            .arg(proxyName));

    // We get a list of matching plugins as proxies can return multiple plugins
    // per file and do not  have a good way of supporting multiple inheritance.
    QList<QObject*> matchingPlugins = proxy->load(filepath);
    if (matchingPlugins.isEmpty()) {
      log::debug("no plugins were returned for proxied candidate '{}' via proxy '{}'",
                QDir::toNativeSeparators(filepath), proxyName);
      printPluginDiagToStderr(
          QString("no plugins were returned for proxied candidate '%1' via proxy '%2'")
              .arg(QDir::toNativeSeparators(filepath))
              .arg(proxyName));
    }

    // IPluginProxy owns one identifier as a unit. Preflight the complete batch
    // before registering or initializing any candidate so a blocked root cannot
    // leave accepted siblings tied to the same Python module generation.
    QSet<QObject*> uniqueMatchingPlugins;
    for (QObject* candidate : matchingPlugins) {
      if (candidate != nullptr) {
        uniqueMatchingPlugins.insert(candidate);
      }
    }

    QHash<QObject*, QString> compatibilityNames;
    for (QObject* candidate : matchingPlugins) {
      if (candidate == nullptr) {
        continue;
      }
      if (auto* plugin = qobject_cast<IPlugin*>(candidate)) {
        const auto decision = preInitCompatibility(plugin, filepath);
        if (decision.nameAvailable) {
          compatibilityNames.insert(candidate, decision.pluginName);
        }
        if (decision.block) {
          reportCompatibilityBlock(candidate, decision.pluginName,
                                   *decision.block);
          PluginCompatibility::retireRejectedProxiedBatch(
              uniqueMatchingPlugins,
              [&] { proxy->unload(filepath); });
          log::debug(
              "proxied identifier '{}' was rejected by compatibility policy",
              QDir::toNativeSeparators(filepath));
          return {};
        }
      }
    }

    // We are going to group plugin by names and "fix" them later:
    std::map<QString, std::vector<IPlugin*>> proxiedByNames;
    QSet<QObject*> acceptedObjects;
    PluginRegistration::ProxiedBatchLedger registrationLedger;

    for (QObject* proxiedPlugin : matchingPlugins) {
      if (proxiedPlugin == nullptr) {
        log::warn("proxy '{}' returned a null QObject for '{}'", proxyName,
                  QDir::toNativeSeparators(filepath));
        printPluginDiagToStderr(
            QString("proxy '%1' returned a null QObject for '%2'")
                .arg(proxyName)
                .arg(QDir::toNativeSeparators(filepath)));
        continue;
      }

      const auto compatibilityName = compatibilityNames.constFind(proxiedPlugin);
      if (compatibilityName == compatibilityNames.cend()) {
        log::warn("proxy '{}' returned a candidate without stable pre-init identity "
                  "for '{}'; rejecting it",
                  proxyName, QDir::toNativeSeparators(filepath));
        continue;
      }
      const std::optional<QString> preflightedName(*compatibilityName);
      const auto batchDecision =
          registrationLedger.begin(proxiedPlugin, preflightedName);
      if (batchDecision == PluginRegistration::BatchDecision::DuplicateObject) {
        log::debug("proxy '{}' returned duplicate QObject for '{}'", proxyName,
                   QDir::toNativeSeparators(filepath));
        continue;
      }
      if (batchDecision ==
          PluginRegistration::BatchDecision::FailedLogicalPlugin) {
        log::debug(
            "skipping additional interface for failed proxied plugin '{}' from '{}'",
            *preflightedName, QDir::toNativeSeparators(filepath));
        continue;
      }

      if (IPlugin* proxied =
              registerPlugin(proxiedPlugin, filepath, proxy,
                             preflightedName);
          proxied) {
        // Record ownership before optional metadata/logging calls so a throw
        // cannot leave a committed registration outside batch bookkeeping.
        proxiedPlugins.push_back(proxiedPlugin);
        acceptedObjects.insert(proxiedPlugin);
        proxiedByNames[*preflightedName].push_back(proxied);
        try {
          log::debug("loaded plugin '{}@{}' from '{}' - [{}]",
                     *preflightedName,
                     proxied->version().canonicalString(),
                     QFileInfo(filepath).fileName(),
                     implementedInterfaces(proxied).join(", "));
        } catch (...) {
          log::debug("loaded plugin '{}' from '{}' (metadata unavailable)",
                     *preflightedName, QFileInfo(filepath).fileName());
        }
      } else {
        registrationLedger.reject(preflightedName);
        log::warn(
            "proxied candidate '{}' from proxy '{}' failed to register as an MO2 plugin",
            QDir::toNativeSeparators(filepath), proxyName);
        printPluginDiagToStderr(
            QString("proxied candidate '%1' from proxy '%2' failed to register as an "
                    "MO2 plugin")
                .arg(QDir::toNativeSeparators(filepath))
                .arg(proxyName));
      }
    }

    // No object from this proxy identifier was admitted, so no requirements
    // entry can lead unloadPlugins() back to it. Retire the identifier now,
    // preserving the F-04 unload-before-holder-destruction ordering.
    if (proxiedPlugins.empty()) {
      PluginCompatibility::retireRejectedProxiedBatch(
          uniqueMatchingPlugins, [&] { proxy->unload(filepath); });
      return {};
    }

    // A mixed identifier remains loaded for its admitted logical plugins.
    // Retain unpublished wrappers until that same identifier is successfully
    // unloaded, then destroy them after the proxy has dropped borrowed handles.
    for (QObject* candidate : uniqueMatchingPlugins) {
      if (!acceptedObjects.contains(candidate)) {
        m_RejectedProxiedObjects.push_back(
            {proxy, QDir::cleanPath(filepath), candidate});
      }
    }

    // Fake masters:
    for (auto& [name, proxiedPlugins] : proxiedByNames) {
      if (proxiedPlugins.size() > 1) {
        auto it = std::min_element(std::begin(proxiedPlugins), std::end(proxiedPlugins),
                                   [&](auto const& lhs, auto const& rhs) {
                                     return isBetterInterface(as_qobject(lhs),
                                                              as_qobject(rhs));
                                   });

        for (auto& proxiedPlugin : proxiedPlugins) {
          if (proxiedPlugin != *it) {
            m_Requirements.at(proxiedPlugin).setMaster(*it);
          }
        }
      }
    }

    log::debug("finished proxied candidate '{}' via proxy '{}': {} plugin(s) loaded",
              QDir::toNativeSeparators(filepath), proxyName,
              proxiedPlugins.size());
    printPluginDiagToStderr(
        QString("finished proxied candidate '%1' via proxy '%2': %3 plugin(s) loaded")
            .arg(QDir::toNativeSeparators(filepath))
            .arg(proxyName)
            .arg(proxiedPlugins.size()));
  } catch (const std::exception& e) {
    log::error("failed to initialize proxied candidate '{}' via proxy '{}': {}",
               QDir::toNativeSeparators(filepath), proxyName, e.what());
    printPluginDiagToStderr(
        QString("failed to initialize proxied candidate '%1' via proxy '%2': %3")
            .arg(QDir::toNativeSeparators(filepath))
            .arg(proxyName)
            .arg(e.what()));
    reportError(
        QObject::tr("failed to initialize plugin %1: %2").arg(filepath).arg(e.what()));
  } catch (...) {
    log::error("failed to initialize proxied candidate '{}' via proxy '{}': "
               "unknown exception",
               QDir::toNativeSeparators(filepath), proxyName);
    printPluginDiagToStderr(
        QString("failed to initialize proxied candidate '%1' via proxy '%2': unknown "
                "exception")
            .arg(QDir::toNativeSeparators(filepath))
            .arg(proxyName));
    reportError(QObject::tr("failed to initialize plugin %1: unknown exception")
                    .arg(filepath));
  }

  return proxiedPlugins;
}

QObject* PluginContainer::loadQtPlugin(const QString& filepath)
{
  std::unique_ptr<QPluginLoader> pluginLoader(new QPluginLoader(filepath, this));
  if (pluginLoader->instance() == nullptr) {
    m_FailedPlugins.push_back(filepath);
    log::error("failed to load plugin {}: {}", filepath, pluginLoader->errorString());
  } else {
    QObject* object = pluginLoader->instance();
    if (IPlugin* plugin = registerPlugin(object, filepath, nullptr); plugin) {
      m_PluginLoaders.push_back(pluginLoader.release());
      try {
        log::debug("loaded plugin '{}@{}' from '{}' - [{}]", plugin->name(),
                   plugin->version().canonicalString(),
                   QFileInfo(filepath).fileName(),
                   implementedInterfaces(plugin).join(", "));
      } catch (...) {
        log::debug("loaded native plugin from '{}' (metadata unavailable)",
                   QFileInfo(filepath).fileName());
      }
      return object;
    } else if (object->property(CompatibilityBlockedProperty).toBool()) {
      log::debug("plugin '{}' was rejected by compatibility policy", filepath);
      if (!pluginLoader->unload()) {
        log::debug("rejected plugin '{}' could not be unloaded immediately: {}",
                   filepath, pluginLoader->errorString());
        // Retain the loader so unloadPlugins() makes a final orderly attempt;
        // QPluginLoader destruction alone neither unloads nor deletes instance().
        m_PluginLoaders.push_back(pluginLoader.release());
      }
    } else {
      m_FailedPlugins.push_back(filepath);
      log::warn("plugin '{}' failed to load (may be outdated)", filepath);
      if (!pluginLoader->unload()) {
        log::debug("failed plugin '{}' could not be unloaded immediately: {}",
                   filepath, pluginLoader->errorString());
        // Registration rollback removed all host state. Retain the loader only
        // so shutdown can retry retiring Qt's still-live root instance.
        m_PluginLoaders.push_back(pluginLoader.release());
      }
    }
  }
  return nullptr;
}

std::optional<QString> PluginContainer::isQtPluginFolder(const QString& filepath)
{

  if (!QFileInfo(filepath).isDir()) {
    return {};
  }

  QDirIterator iter(filepath, QDir::Files | QDir::NoDotAndDotDot);
  while (iter.hasNext()) {
    iter.next();
    const auto filePath = iter.filePath();

    // not a library, skip
    if (!QLibrary::isLibrary(filePath)) {
      continue;
    }

    // check if we have proper metadata - this does not load the plugin (metaData()
    // should be very lightweight)
    const QPluginLoader loader(filePath);
    if (!loader.metaData().isEmpty()) {
      return filePath;
    }
  }

  return {};
}

void PluginContainer::loadPlugin(QString const& filepath)
{
  std::vector<QObject*> plugins;
  if (QFileInfo(filepath).isFile() && QLibrary::isLibrary(filepath)) {
    QObject* plugin = loadQtPlugin(filepath);
    if (plugin) {
      plugins.push_back(plugin);
    }
  } else if (auto p = isQtPluginFolder(filepath)) {
    QObject* plugin = loadQtPlugin(*p);
    if (plugin) {
      plugins.push_back(plugin);
    }
  } else {
    // We need to check if this can be handled by a proxy.
    for (auto* proxy : this->plugins<IPluginProxy>()) {
      auto filepaths = mergedProxyList(proxy);
      if (filepaths.contains(filepath)) {
        plugins = loadProxied(filepath, proxy);
        break;
      }
    }
  }

  for (auto* plugin : plugins) {
    emit pluginRegistered(qobject_cast<IPlugin*>(plugin));
  }

  startPluginsImpl(plugins);
}

void PluginContainer::unloadPlugin(QString const& filepath)
{
  const QString cleanPath = QDir::cleanPath(filepath);
  const auto& objects     = bf::at_key<QObject>(m_Plugins);
  const bool loaded = std::any_of(objects.begin(), objects.end(), [&](auto* object) {
    auto* plugin = qobject_cast<IPlugin*>(object);
    return plugin != nullptr && this->filepath(plugin) == cleanPath;
  });
  if (loaded) {
    log::error("refusing to live-unload plugin '{}'; restart Mod Organizer to "
               "retire the loaded generation safely",
               cleanPath);
  }
}

bool PluginContainer::reloadPlugin(QString const& filepath)
{
  const QString cleanPath = QDir::cleanPath(filepath);
  const auto& objects     = bf::at_key<QObject>(m_Plugins);
  const bool loaded = std::any_of(objects.begin(), objects.end(), [&](auto* object) {
    auto* plugin = qobject_cast<IPlugin*>(object);
    return plugin != nullptr && this->filepath(plugin) == cleanPath;
  });
  if (pluginReloadDecision(loaded) == PluginReloadDecision::RestartRequired) {
    log::error("refusing to live-reload plugin '{}'; restart Mod Organizer to "
               "load a new generation safely",
               cleanPath);
    return false;
  }

  loadPlugin(cleanPath);
  return true;
}

void PluginContainer::unloadPlugins()
{
  if (m_Organizer) {
    // this will clear several structures that can hold on to pointers to
    // plugins, as well as read the plugin blacklist from the ini file, which
    // is used in loadPlugins() below to skip plugins
    //
    // note that the first thing loadPlugins() does is call unloadPlugins(),
    // so this makes sure the blacklist is always available
    m_Organizer->settings().plugins().clearPlugins();
  }

  QSet<QString> seenProxiedPaths;
  std::vector<std::pair<IPluginProxy*, QString>> proxiedPaths;
  const auto objects = bf::at_key<QObject>(m_Plugins);
  for (QObject* object : objects) {
    auto* plugin = qobject_cast<IPlugin*>(object);
    if (!plugin) {
      continue;
    }

    const auto req = m_Requirements.find(plugin);
    if (req == m_Requirements.end()) {
      continue;
    }

    if (auto* proxy = req->second.m_Organizer) {
      proxy->disconnectSignals();
    }

    if (auto* game = qobject_cast<IPluginGame*>(object)) {
      unregisterGame(game);
    }

    auto* pluginProxy = req->second.proxy();
    if (pluginProxy) {
      const QString path = filepath(plugin);
      if (!seenProxiedPaths.contains(path)) {
        proxiedPaths.emplace_back(pluginProxy, path);
        seenProxiedPaths.insert(path);
      }
    }
  }

  for (const auto& [proxy, path] : proxiedPaths) {
    bool unloaded = false;
    try {
      proxy->unload(path);
      unloaded = true;
    } catch (const std::exception& e) {
      log::error("failed to unload proxied plugin '{}': {}", path, e.what());
    } catch (...) {
      log::error("failed to unload proxied plugin '{}': unknown exception", path);
    }

    if (unloaded) {
      for (auto& rejected : m_RejectedProxiedObjects) {
        if (rejected.proxy == proxy && rejected.filepath == path &&
            !rejected.object.isNull()) {
          delete rejected.object;
        }
      }
    }
  }
  // On an unload exception, deliberately leave surviving holder objects alive
  // rather than invalidating borrowed proxy-runtime handles.
  m_RejectedProxiedObjects.clear();

  bf::for_each(m_Plugins, [](auto& t) {
    t.second.clear();
  });
  bf::for_each(m_AccessPlugins, [](auto& t) {
    t.second.clear();
  });
  m_Requirements.clear();

  while (!m_PluginLoaders.empty()) {
    QPluginLoader* loader = m_PluginLoaders.back();
    m_PluginLoaders.pop_back();
    if (loader != nullptr) {
      try {
        if (!loader->unload()) {
          log::debug("failed to unload {}: {}", loader->fileName(),
                     loader->errorString());
        }
      } catch (const std::exception& e) {
        log::error("failed to unload {}: {}", loader->fileName(), e.what());
      } catch (...) {
        log::error("failed to unload {}: unknown exception", loader->fileName());
      }
    }
    delete loader;
  }

}

void PluginContainer::loadPlugins()
{
  TimeThis tt("PluginContainer::loadPlugins()");

  unloadPlugins();

  for (QObject* plugin : QPluginLoader::staticInstances()) {
    registerPlugin(plugin, "", nullptr);
  }

  QFile loadCheck;
  QString skipPlugin;

  if (m_Organizer) {
    loadCheck.setFileName(qApp->property("dataPath").toString() +
                          "/plugin_loadcheck.tmp");

    if (loadCheck.exists() && loadCheck.open(QIODevice::ReadOnly | QIODevice::Text)) {
      // oh, there was a failed plugin load last time. Find out which plugin was loaded
      // last
      const auto contents = loadCheck.readAll();
      loadCheck.close();

      const auto lines = QString::fromUtf8(contents).split('\n', Qt::SkipEmptyParts);
      const auto fileName = lines.isEmpty() ? QString() : lines.last().trimmed();

      log::warn("loadcheck file found for plugin '{}'", fileName);

      MOBase::TaskDialog dlg;

      const auto Skip      = QMessageBox::Ignore;
      const auto Blacklist = QMessageBox::Cancel;
      const auto Load      = QMessageBox::Ok;

      const auto r =
          dlg.title(tr("Plugin error"))
              .main(tr("Mod Organizer failed to load the plugin '%1' last time it was "
                       "started.")
                        .arg(fileName))
              .content(tr(
                  "The plugin can be skipped for this session, blacklisted, "
                  "or loaded normally, in which case it might fail again. Blacklisted "
                  "plugins can be re-enabled later in the settings."))
              .icon(QMessageBox::Warning)
              .button({tr("Skip this plugin"), Skip})
              .button({tr("Blacklist this plugin"), Blacklist})
              .button({tr("Load this plugin"), Load})
              .exec();

      switch (r) {
      case Skip:
        log::warn("user wants to skip plugin '{}'", fileName);
        skipPlugin = fileName;
        break;

      case Blacklist:
        log::warn("user wants to blacklist plugin '{}'", fileName);
        m_Organizer->settings().plugins().addBlacklist(fileName);
        break;

      case Load:
        log::warn("user wants to load plugin '{}' anyway", fileName);
        break;

      default:
        break;
      }
    }

    if (!loadCheck.open(QIODevice::WriteOnly)) {
      log::warn("failed to open loadcheck file for writing '{}'",
                QDir::toNativeSeparators(loadCheck.fileName()));
    }
  }

  m_BundledPluginPath = AppConfig::pluginsPath();

  if (m_Organizer) {
    QString instancePluginPath =
        QDir(QDir::fromNativeSeparators(m_Organizer->basePath())).filePath("plugins");
    if (QDir::cleanPath(instancePluginPath) != QDir::cleanPath(m_BundledPluginPath)) {
      QDir().mkpath(instancePluginPath);
      m_PluginPath = instancePluginPath;
      log::debug("instance plugin directory: {}",
                 QDir::toNativeSeparators(m_PluginPath));

      // Migration: remove stale symlinks left by the old
      // ensureBundledPluginsLinked() approach. Only symlinks are removed; real
      // user files are left untouched.
      QDirIterator cleanIter(instancePluginPath,
                             QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
      while (cleanIter.hasNext()) {
        cleanIter.next();
        if (QFileInfo(cleanIter.filePath()).isSymLink()) {
          log::debug("removing stale plugin symlink '{}'",
                     QDir::toNativeSeparators(cleanIter.filePath()));
          QFile::remove(cleanIter.filePath());
        }
      }
    } else {
      m_PluginPath = m_BundledPluginPath;
    }
  } else {
    m_PluginPath = m_BundledPluginPath;
  }

  log::debug("bundled plugins: {}", QDir::toNativeSeparators(m_BundledPluginPath));
  log::debug("looking for plugins in {}", QDir::toNativeSeparators(m_PluginPath));

  // Linux is case-sensitive; keep only the canonical Fallout NV plugin filename.
  // Older builds may leave a stale lowercase artifact that causes duplicate
  // registration warnings at startup.
  auto cleanStaleNvPlugin = [](const QString& dir) {
    const QString nvCanonical = dir + "/libgame_falloutNV.so";
    const QString nvStale     = dir + "/libgame_falloutnv.so";
    if (QFile::exists(nvCanonical) && QFile::exists(nvStale)) {
      if (QFile::remove(nvStale)) {
        log::debug("removed stale plugin artifact '{}'",
                   QDir::toNativeSeparators(nvStale));
      } else {
        log::warn("failed to remove stale plugin artifact '{}'",
                  QDir::toNativeSeparators(nvStale));
      }
    }
  };
  cleanStaleNvPlugin(m_BundledPluginPath);
  if (m_PluginPath != m_BundledPluginPath) {
    cleanStaleNvPlugin(m_PluginPath);
  }

  // FOMOD Plus was bundled in earlier Fluorine releases but has since been
  // retired. Portable/in-place updates do not remove files that disappeared
  // from a newer archive, and old per-instance plugin copies can survive too.
  // Remove only the three known executable artifacts before plugin discovery;
  // preserve fomod.db and settings because those are user data.
  const QSet<QString> retiredFomodPlusArtifacts = {
      "libfomod_plus_installer.so",
      "libfomod_plus_scanner.so",
      "libfomod_plus_patch_wizard.so",
      "libfomod_plus_installer.dylib",
      "libfomod_plus_scanner.dylib",
      "libfomod_plus_patch_wizard.dylib",
      "fomod_plus_installer.dll",
      "fomod_plus_scanner.dll",
      "fomod_plus_patch_wizard.dll",
  };
  auto cleanRetiredFomodPlus = [&retiredFomodPlusArtifacts](const QString& dir) {
    QDirIterator iter(dir, QDir::Files | QDir::System | QDir::NoDotAndDotDot);
    while (iter.hasNext()) {
      iter.next();
      if (!retiredFomodPlusArtifacts.contains(iter.fileName().toLower())) {
        continue;
      }
      if (QFile::remove(iter.filePath())) {
        log::info("removed retired FOMOD Plus plugin '{}'",
                  QDir::toNativeSeparators(iter.filePath()));
      } else {
        log::warn("failed to remove retired FOMOD Plus plugin '{}'",
                  QDir::toNativeSeparators(iter.filePath()));
      }
    }
  };
  cleanRetiredFomodPlus(m_BundledPluginPath);
  if (m_PluginPath != m_BundledPluginPath) {
    cleanRetiredFomodPlus(m_PluginPath);
  }

  // Build merged plugin map: instance extras first (low priority),
  // then bundled plugins overwrite (high priority).
  QMap<QString, QString> pluginMap;  // filename -> full path

  if (m_PluginPath != m_BundledPluginPath) {
    QDirIterator instanceIter(m_PluginPath,
                              QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    while (instanceIter.hasNext()) {
      instanceIter.next();
      pluginMap[instanceIter.fileName()] = instanceIter.filePath();
    }
  }

  QDirIterator bundledIter(m_BundledPluginPath,
                           QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  while (bundledIter.hasNext()) {
    bundledIter.next();
    pluginMap[bundledIter.fileName()] = bundledIter.filePath();
  }

  // Plugins that are permanently blacklisted for all users.  These are
  // known-incompatible with Fluorine / Linux and must never be loaded.
  static const QSet<QString> hardBlacklist = {
      QStringLiteral("rootbuilder"),
  };

  for (auto it = pluginMap.cbegin(); it != pluginMap.cend(); ++it) {
    const QString& fileName = it.key();
    const QString& filepath = it.value();

    if (hardBlacklist.contains(fileName.toLower())) {
      log::debug("plugin \"{}\" is hard-blacklisted, skipping", fileName);
      continue;
    }

    if (skipPlugin == fileName) {
      log::debug("plugin \"{}\" skipped for this session", fileName);
      continue;
    }

    if (m_Organizer) {
      if (m_Organizer->settings().plugins().blacklisted(fileName)) {
        log::debug("plugin \"{}\" blacklisted", fileName);
        continue;
      }
    }

    if (loadCheck.isOpen()) {
      loadCheck.write(fileName.toUtf8());
      loadCheck.write("\n");
      loadCheck.flush();
    }

    if (QLibrary::isLibrary(filepath)) {
      loadQtPlugin(filepath);
    } else if (auto p = isQtPluginFolder(filepath)) {
      loadQtPlugin(*p);
    }
  }

  if (skipPlugin.isEmpty()) {
    // remove the load check file on success
    if (loadCheck.isOpen()) {
      loadCheck.remove();
    }
  } else {
    // remember the plugin for next time
    if (loadCheck.isOpen()) {
      loadCheck.close();
    }

    log::warn("user skipped plugin '{}', remembering in loadcheck", skipPlugin);
    if (loadCheck.open(QIODevice::WriteOnly)) {
      loadCheck.write(skipPlugin.toUtf8());
      loadCheck.write("\n");
      loadCheck.flush();
    } else {
      log::warn("failed to persist skipped plugin to '{}'",
                QDir::toNativeSeparators(loadCheck.fileName()));
    }
  }

  bf::at_key<IPluginDiagnose>(m_Plugins).push_back(this);

  if (m_Organizer) {
    bf::at_key<IPluginDiagnose>(m_Plugins).push_back(m_Organizer);
    m_Organizer->connectPlugins(this);
  }
}

std::vector<unsigned int> PluginContainer::activeProblems() const
{
  std::vector<unsigned int> problems;
  if (!m_FailedPlugins.empty()) {
    problems.push_back(PROBLEM_PLUGINSNOTLOADED);
  }
  return problems;
}

QString PluginContainer::shortDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_PLUGINSNOTLOADED: {
    return tr("Some plugins could not be loaded");
  } break;
  default: {
    return tr("Description missing");
  } break;
  }
}

QString PluginContainer::fullDescription(unsigned int key) const
{
  switch (key) {
  case PROBLEM_PLUGINSNOTLOADED: {
    QString result =
        tr("The following plugins could not be loaded. The reason may be missing "
           "dependencies (i.e. python) or an outdated version:") +
        "<ul>";
    for (const QString& plugin : m_FailedPlugins) {
      result += "<li>" + plugin + "</li>";
    }
    result += "<ul>";
    return result;
  } break;
  default: {
    return tr("Description missing");
  } break;
  }
}

bool PluginContainer::hasGuidedFix(unsigned int) const
{
  return false;
}

void PluginContainer::startGuidedFix(unsigned int) const {}

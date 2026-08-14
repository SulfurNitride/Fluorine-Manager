#include "PluginList.h"
#include "FileConflictParser.h"
#include "TESFile/Reader.h"

#include <bsatk/bsatk.h>
#include <game_features/gameplugins.h>
#include <game_features/igamefeatures.h>
#include <log.h>
#include <uibase/transactionalwritefile.h>
#include <utility.h>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/thread/future.hpp>

#include <QDir>
#include <QFile>
#include <QStringTokenizer>
#include <QTextStream>

#include <algorithm>
#include <future>
#include <iterator>
#include <limits>
#include <ranges>
#include <semaphore>
#include <thread>
#include <tuple>
#include <utility>

using namespace Qt::Literals::StringLiterals;

namespace TESData
{

#pragma region Constructor / Destructor

PluginList::PluginList(const MOBase::IOrganizer* moInfo) : m_Organizer{moInfo}
{
  refresh(true);
}

PluginList::~PluginList() noexcept
{
  m_Refreshed.disconnect_all_slots();
  m_PluginMoved.disconnect_all_slots();
  m_PluginStateChanged.disconnect_all_slots();
}

#pragma endregion Constructor / Destructor
#pragma region Plugin Access

int PluginList::pluginCount() const
{
  return static_cast<int>(m_Plugins.size());
}

int PluginList::getIndex(const QString& pluginName) const
{
  const auto it = m_PluginsByName.find(pluginName);
  return it != m_PluginsByName.end() ? it->second : -1;
}

int PluginList::getIndexAtPriority(int priority) const
{
  return m_PluginsByPriority.at(priority);
}

QString PluginList::getOriginName(int index) const
{
  if (index < 0 || index >= m_Plugins.size()) {
    return QString();
  }

  const auto& name   = m_Plugins[index]->name();
  const auto origins = m_Organizer->getFileOrigins(name);
  return !origins.isEmpty() ? origins.first() : QString();
}

const FileInfo* PluginList::getPlugin(int index) const
{
  if (index < 0 || index >= m_Plugins.size()) {
    return nullptr;
  }

  return m_Plugins[index].get();
}

const FileInfo* PluginList::getPluginByName(const QString& name) const
{
  const auto it = m_PluginsByName.find(name);
  if (it == m_PluginsByName.end()) {
    return nullptr;
  } else {
    return m_Plugins.at(it->second).get();
  }
}

const FileInfo* PluginList::getPluginByPriority(int priority) const
{
  if (priority < 0 || priority >= m_PluginsByPriority.size()) {
    return nullptr;
  }

  return m_Plugins.at(m_PluginsByPriority[priority]).get();
}

FileInfo* PluginList::findPlugin(const QString& name)
{
  const auto it = m_PluginsByName.find(name);
  if (it == m_PluginsByName.end()) {
    return nullptr;
  } else {
    return m_Plugins.at(it->second).get();
  }
}

const FileInfo* PluginList::findPlugin(const QString& name) const
{
  const auto it = m_PluginsByName.find(name);
  if (it == m_PluginsByName.end()) {
    return nullptr;
  } else {
    return m_Plugins.at(it->second).get();
  }
}

#pragma endregion Plugin Access
#pragma region Record Access

FileEntry* PluginList::findEntryByName(const std::string& pluginName) const
{
  std::shared_lock lk{m_FileEntryMutex};
  const auto it = m_EntriesByName.find(pluginName);
  return it != m_EntriesByName.end() ? it->second.get() : nullptr;
}

FileEntry* PluginList::findEntryByHandle(TESFileHandle handle) const
{
  std::shared_lock lk{m_FileEntryMutex};
  const auto it = m_EntriesByHandle.find(handle);
  return it != m_EntriesByHandle.end() ? it->second.get() : nullptr;
}

AssociatedEntry* PluginList::findArchive(const QString& name) const
{
  std::shared_lock lk{m_ArchiveEntryMutex};
  const auto it = m_Archives.find(name);
  return it != m_Archives.end() ? it->second.get() : nullptr;
}

FileEntry* PluginList::createEntry(const std::string& name)
{
  std::unique_lock lk{m_FileEntryMutex};

  const auto it = m_EntriesByName.find(name);
  if (it != m_EntriesByName.end()) {
    return it->second.get();
  }

  const auto entry = std::make_shared<FileEntry>(m_NextHandle++, name);
  m_EntriesByHandle.emplace_hint(m_EntriesByHandle.cend(), entry->handle(), entry);
  m_EntriesByName[name] = entry;
  return entry.get();
}

void PluginList::addRecordConflict(const std::string& pluginName,
                                   const RecordPath& path, TESFile::Type type,
                                   const std::string& name)
{
  const auto& master = path.hasFormId() ? path.files()[path.formId() >> 24] : "";
  const auto owner   = createEntry(master);
  const auto record  = owner->createRecord(path, name, type);
  if (pluginName != master) {
    const auto entry = createEntry(pluginName);
    entry->addRecord(path, name, type, record);
  }
}

void PluginList::addGroupPlaceholder(const std::string& pluginName,
                                     const RecordPath& path)
{
  const auto group   = path.groups().back();
  const auto& master = group.hasParent() ? path.files()[group.parent() >> 24] : "";
  if (const auto owner = findEntryByName(master)) {
    owner->addChildGroup(path);
  }
  if (pluginName != master) {
    if (const auto entry = findEntryByName(pluginName)) {
      entry->addChildGroup(path);
    }
  }
}

#pragma endregion Record Access
#pragma region List Management

QString PluginList::groupsPath() const
{
  const auto profilePath = QDir(m_Organizer->profilePath());
  if (profilePath.isEmpty()) {
    return QString();
  }

  return QDir::cleanPath(profilePath.absoluteFilePath(u"plugingroups.txt"_s));
}

QString PluginList::lockedOrderPath() const
{
  const auto profilePath = QDir(m_Organizer->profilePath());
  if (profilePath.isEmpty()) {
    return QString();
  }

  return QDir::cleanPath(profilePath.absoluteFilePath(u"lockedorder.txt"_s));
}

void PluginList::refresh(bool invalidate)
{
  MOBase::TimeThis tt{"TESData::PluginList::refresh()"};

  m_Refreshing = true;
  scanDataFiles(invalidate);
  readPluginLists();

  if (const auto groupsFile = groupsPath(); !groupsFile.isEmpty()) {
    readGroups(groupsFile);
  }

  computeCompileIndices();
  refreshLoadOrder();
  dispatchPluginStateChanges();
  testMasters();

  if (const auto lockedOrderFile = lockedOrderPath(); !lockedOrderFile.isEmpty()) {
    writeEmptyTextFile(lockedOrderFile);
  }

  m_Refreshing = false;
  m_Refreshed();
}

void PluginList::setEnabled(int id, bool enable)
{
  const auto plugin = m_Plugins.at(id);

  const bool enabled = plugin->enabled();
  const bool shouldEnable =
      (enable && !plugin->forceDisabled()) || plugin->isAlwaysEnabled();

  if (shouldEnable != enabled) {
    plugin->setEnabled(shouldEnable);
    computeCompileIndices();
    refreshLoadOrder();
    pluginStatesChanged({plugin->name()}, shouldEnable ? STATE_ACTIVE : STATE_INACTIVE);
    testMasters();
  }
}

void PluginList::setEnabled(const std::vector<int>& ids, bool enable)
{
  QStringList changed;
  for (const int id : ids) {
    const auto plugin = m_Plugins.at(id);

    const bool enabled = plugin->enabled();
    const bool shouldEnable =
        (enable && !plugin->forceDisabled()) || plugin->isAlwaysEnabled();

    if (shouldEnable != enabled) {
      plugin->setEnabled(shouldEnable);
      changed.append(plugin->name());
    }
  }

  if (!changed.isEmpty()) {
    computeCompileIndices();
    refreshLoadOrder();
    pluginStatesChanged(changed, enable ? STATE_ACTIVE : STATE_INACTIVE);
    testMasters();
  }
}

void PluginList::toggleState(const std::vector<int>& ids)
{
  QStringList active;
  QStringList inactive;
  for (const int id : ids) {
    const auto plugin = m_Plugins.at(id);

    const bool enabled = plugin->enabled();
    const bool shouldEnable =
        (!enabled && !plugin->forceDisabled()) || plugin->isAlwaysEnabled();

    if (shouldEnable != enabled) {
      plugin->setEnabled(shouldEnable);
      if (shouldEnable) {
        active.append(plugin->name());
      } else {
        inactive.append(plugin->name());
      }
    }
  }

  if (!active.isEmpty() || !inactive.isEmpty()) {
    computeCompileIndices();
    refreshLoadOrder();
    if (!active.isEmpty()) {
      pluginStatesChanged(active, STATE_ACTIVE);
    }
    if (!inactive.isEmpty()) {
      pluginStatesChanged(inactive, STATE_INACTIVE);
    }
    testMasters();
  }
}

bool PluginList::canMoveToPriority(const std::vector<int>& ids, int newPriority) const
{
  boost::container::flat_set<QString, MOBase::FileNameComparator> names;
  names.reserve(ids.size());
  for (const int id : ids) {
    const auto plugin = m_Plugins[id];
    names.insert(plugin->name());
  }

  for (const int id : ids) {
    const auto pluginToMove = m_Plugins[id];
    const int priority      = pluginToMove->priority();

    if (pluginToMove->forceLoaded()) {
      const int min = std::min(priority, newPriority);
      const int max = std::max(priority, newPriority);
      for (int i = min; i < max; ++i) {
        if (std::ranges::find(ids, m_PluginsByPriority.at(i)) == std::end(ids)) {
          return false;
        }
      }
    }

    for (int i = newPriority; i < priority; ++i) {
      const auto plugin = m_Plugins.at(m_PluginsByPriority.at(i));
      if (!names.contains(plugin->name()) && pluginToMove->mustLoadAfter(*plugin)) {
        return false;
      }
    }

    for (int i = priority + 1; i < newPriority; ++i) {
      const auto plugin = m_Plugins.at(m_PluginsByPriority.at(i));
      if (!names.contains(plugin->name()) && plugin->mustLoadAfter(*pluginToMove)) {
        return false;
      }
    }
  }

  return true;
}

QString PluginList::destinationGroup(
    int oldPriority, int newPriority, const QString& originalGroup, bool isESM,
    const boost::container::flat_set<QString, MOBase::FileNameComparator>& exclusions)
{
  const auto findPrevious = [&, this](int priority) -> FileInfo* {
    for (int i = priority - 1; i >= 0; --i) {
      const auto& plugin = m_Plugins.at(m_PluginsByPriority[i]);
      if (!exclusions.contains(plugin->name())) {
        return plugin.get();
      }
    }
    return nullptr;
  };

  const auto findNext = [&, this](int priority) -> FileInfo* {
    for (int i = priority + 1; i < m_PluginsByPriority.size(); ++i) {
      const auto& plugin = m_Plugins.at(m_PluginsByPriority[i]);
      if (!exclusions.contains(plugin->name())) {
        return plugin.get();
      }
    }
    return nullptr;
  };

  bool removedFromGroup = false;
  if (!originalGroup.isEmpty()) {
    if (const auto previous = findPrevious(oldPriority)) {
      if (previous->group() == originalGroup && previous->isMasterFile() == isESM) {
        removedFromGroup = true;
      }
    }

    if (const auto next = findNext(oldPriority)) {
      if (next->group() == originalGroup && next->isMasterFile() == isESM) {
        removedFromGroup = true;
      }
    }
  }

  QString displacedGroup;
  if (const auto displaced = m_Plugins.at(m_PluginsByPriority[newPriority])) {
    displacedGroup = displaced->group();
  }

  QString neighborGroup;
  if (newPriority < oldPriority) {
    if (const auto neighbor = findPrevious(newPriority)) {
      neighborGroup = neighbor->group();
    }
  } else if (newPriority > oldPriority) {
    if (const auto neighbor = findNext(newPriority)) {
      neighborGroup = neighbor->group();
    }
  }

  if (!displacedGroup.isEmpty() && neighborGroup == displacedGroup) {
    return displacedGroup;
  }

  if (!removedFromGroup || displacedGroup == originalGroup) {
    return originalGroup;
  }

  return QString();
}

void PluginList::moveToPriority(std::vector<int> ids, int destination, bool disjoint)
{
  if (ids.empty()) {
    return;
  }

  destination = std::max(destination, 0);
  destination = std::min(destination, pluginCount());

  boost::container::flat_set<QString, MOBase::FileNameComparator> names;
  names.reserve(ids.size());
  boost::container::flat_map<QString, int> priorities;
  priorities.reserve(ids.size());
  for (const int id : ids) {
    const auto plugin = m_Plugins.at(id);
    names.insert(plugin->name());
    priorities.emplace(plugin->name(), plugin->priority());
  }

  std::ranges::sort(ids, [this](int lhs, int rhs) {
    return m_Plugins[lhs]->priority() > m_Plugins[rhs]->priority();
  });

  int nextDestination = destination;
  for (const int id : ids) {
    const auto& pluginToMove = m_Plugins[id];
    const int priority       = pluginToMove->priority();

    if (nextDestination < priority) {
      for (int i = priority - 1; i >= nextDestination; --i) {
        const auto& plugin = m_Plugins.at(m_PluginsByPriority.at(i));
        if (!names.contains(plugin->name()) && pluginToMove->mustLoadAfter(*plugin)) {
          nextDestination = i + 1;
          break;
        }
      }

      const int newPriority = nextDestination;
      pluginToMove->setGroup(destinationGroup(priority, newPriority,
                                              pluginToMove->group(),
                                              pluginToMove->isMasterFile(), names));
      for (int i = priority; i > nextDestination; --i) {
        m_PluginsByPriority[i] = m_PluginsByPriority[i - 1];
        m_Plugins[m_PluginsByPriority[i]]->setPriority(i);
      }

      m_PluginsByPriority[newPriority] = id;
      pluginToMove->setPriority(newPriority);
    } else if (nextDestination > priority) {
      for (int i = priority + 1; i < nextDestination; ++i) {
        const auto& plugin = m_Plugins.at(m_PluginsByPriority.at(i));
        if (!names.contains(plugin->name()) && plugin->mustLoadAfter(*pluginToMove)) {
          nextDestination = i;
          break;
        }
      }

      const int newPriority = --nextDestination;
      pluginToMove->setGroup(destinationGroup(priority, newPriority,
                                              pluginToMove->group(),
                                              pluginToMove->isMasterFile(), names));
      for (int i = priority; i < newPriority; ++i) {
        m_PluginsByPriority[i] = m_PluginsByPriority[i + 1];
        m_Plugins[m_PluginsByPriority[i]]->setPriority(i);
      }

      m_PluginsByPriority[newPriority] = id;
      pluginToMove->setPriority(newPriority);
    }

    if (disjoint) {
      nextDestination = destination;
    }
  }

  computeCompileIndices();
  refreshLoadOrder();

  boost::container::flat_map<int, std::tuple<QString, int>> movedUp;
  boost::container::flat_map<int, std::tuple<QString, int>, std::greater<int>>
      movedDown;

  for (const int id : ids) {
    const auto& plugin    = m_Plugins[id];
    const auto& name      = plugin->name();
    const int oldPriority = priorities[name];
    const int newPriority = plugin->priority();
    if (newPriority < oldPriority) {
      movedUp[oldPriority] = std::make_tuple(name, newPriority);
    } else if (newPriority > oldPriority) {
      movedDown[oldPriority] = std::make_tuple(name, newPriority);
    }
  }

  for (auto& [oldPriority, moveInfo] : movedDown) {
    auto& [name, newPriority] = moveInfo;
    m_PluginMoved(name, oldPriority, newPriority);
  }

  for (auto& [oldPriority, moveInfo] : movedUp) {
    auto& [name, newPriority] = moveInfo;
    m_PluginMoved(name, oldPriority, newPriority);
  }
}

void PluginList::shiftPriority(const std::vector<int>& ids, int offset)
{
  if (offset < 0) {
    const int minPriority =
        std::ranges::min(std::ranges::transform_view(ids, [this](auto&& id) {
          return m_Plugins.at(id)->priority();
        }));
    moveToPriority(ids, minPriority + offset);
  } else if (offset > 0) {
    const int maxPriority =
        std::ranges::max(std::ranges::transform_view(ids, [this](auto&& id) {
          return m_Plugins.at(id)->priority();
        }));
    moveToPriority(ids, maxPriority + 1 + offset);
  }
}

void PluginList::setGroup(const std::vector<int>& ids, const QString& group)
{
  for (const int id : ids) {
    m_Plugins.at(id)->setGroup(group);
  }

  if (const auto groupsFile = groupsPath(); !groupsFile.isEmpty()) {
    writeGroups(groupsFile);
  }
}

QStringList PluginList::loadOrder() const
{
  QStringList result;

  for (const int id : m_PluginsByPriority) {
    result.append(m_Plugins.at(id)->name());
  }

  return result;
}

void PluginList::notifyPendingState(const QString& mod,
                                    MOBase::IModList::ModStates state)
{
  if (state & MOBase::IModList::STATE_ACTIVE) {
    m_PendingActive.insert(mod);
  } else {
    m_PendingActive.erase(mod);
  }
}

void PluginList::flushPendingStates()
{
  m_PendingActive.clear();
}

#pragma endregion List Management
#pragma region IPluginList

QStringList PluginList::pluginNames() const
{
  QStringList result;

  for (const auto& info : m_Plugins) {
    result.append(info->name());
  }

  return result;
}

MOBase::IPluginList::PluginStates PluginList::state(const QString& name) const
{
  const auto plugin = findPlugin(name);
  if (!plugin) {
    return IPluginList::STATE_MISSING;
  } else {
    return plugin->enabled() ? IPluginList::STATE_ACTIVE : IPluginList::STATE_INACTIVE;
  }
}

void PluginList::setState(const QString& name, PluginStates state)
{
  const auto it = m_PluginsByName.find(name);
  if (it == m_PluginsByName.end()) {
    return;
  }

  const auto plugin = m_Plugins.at(it->second).get();

  if (state == STATE_MISSING) {
    m_Plugins.erase(m_Plugins.begin() + it->second);
  } else {
    const bool enabled      = plugin->enabled();
    const bool shouldEnable = (state == STATE_ACTIVE && !plugin->forceDisabled()) ||
                              plugin->isAlwaysEnabled();
    if (shouldEnable == enabled) {
      return;
    }
    plugin->setEnabled(shouldEnable);
  }

  queuePluginStateChange(plugin->name(), state);
}

int PluginList::priority(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->priority() : -1;
}

bool PluginList::setPriority(const QString& name, int newPriority)
{
  if (newPriority < 0 || newPriority >= static_cast<int>(m_PluginsByPriority.size())) {
    MOBase::log::warn("Requested priority for \"{}\" out of range: {}", name,
                      newPriority);
    return false;
  }

  const int oldPriority = priority(name);
  if (oldPriority == -1) {
    return false;
  }

  const int rowIndex = m_PluginsByPriority.at(oldPriority);

  int destination = newPriority;
  if (newPriority > oldPriority) {
    ++destination;
  }
  moveToPriority({rowIndex}, destination);

  return true;
}

int PluginList::loadOrder(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->loadOrder() : -1;
}

void PluginList::setLoadOrder(const QStringList& pluginList)
{
  for (const auto& info : m_Plugins) {
    info->setPriority(-1);
  }

  int maxPriority = 0;
  for (const QString& pluginName : pluginList) {
    const auto plugin = const_cast<TESData::FileInfo*>(findPlugin(pluginName));
    if (plugin != nullptr) {
      plugin->setPriority(maxPriority++);
    }
  }

  for (const auto& info : m_Plugins) {
    if (info->priority() == -1) {
      info->setPriority(maxPriority++);
    }
  }

  updateCache();
}

bool PluginList::isMaster(const QString& name) const
{
  return isMasterFlagged(name);
}

QStringList PluginList::masters(const QString& name) const
{
  const auto plugin = findPlugin(name);
  QStringList result;
  if (plugin != nullptr) {
    for (const QString& master : plugin->masters()) {
      result.append(master);
    }
  }
  return result;
}

QString PluginList::origin(const QString& name) const
{
  if (m_PluginsByName.find(name) == m_PluginsByName.end()) {
    return QString();
  }

  const auto origins = m_Organizer->getFileOrigins(name);
  return !origins.isEmpty() ? origins.first() : QString();
}

bool PluginList::onRefreshed(const std::function<void()>& callback)
{
  auto connection = m_Refreshed.connect(callback);
  return connection.connected();
}

bool PluginList::onPluginMoved(
    const std::function<void(const QString&, int, int)>& func)
{
  auto connection = m_PluginMoved.connect(func);
  return connection.connected();
}

bool PluginList::onPluginStateChanged(
    const std::function<void(const std::map<QString, PluginStates>&)>& func)
{
  auto connection = m_PluginStateChanged.connect(func);
  return connection.connected();
}

bool PluginList::hasMasterExtension(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->hasMasterExtension() : false;
}

bool PluginList::hasLightExtension(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->hasLightExtension() : false;
}

bool PluginList::isMasterFlagged(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->isMasterFlagged() : false;
}

bool PluginList::isMediumFlagged(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->isMediumFlagged() : false;
}

bool PluginList::isLightFlagged(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->isLightFlagged() : false;
}

bool PluginList::isBlueprintFlagged(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->isBlueprintFlagged() : false;
}

bool PluginList::hasNoRecords(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->hasNoRecords() : false;
}

int PluginList::formVersion(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->formVersion() : 0;
}

float PluginList::headerVersion(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->headerVersion() : 0.0;
}

QString PluginList::author(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->author() : QString();
}

QString PluginList::description(const QString& name) const
{
  const auto plugin = findPlugin(name);
  return plugin ? plugin->description() : QString();
}

#pragma endregion IPluginList
#pragma region ILootCache

void PluginList::clearAdditionalInformation()
{
  m_LootInfo.clear();
}

void PluginList::addLootReport(const QString& name, MOTools::Loot::Plugin plugin)
{
  const auto it = m_PluginsByName.find(name);
  if (it != m_PluginsByName.end()) {
    m_LootInfo[name] = std::move(plugin);
  } else {
    MOBase::log::warn("failed to associate loot report for \"{}\"", name);
  }
}

const MOTools::Loot::Plugin* PluginList::getLootReport(const QString& name) const
{
  const auto it = m_LootInfo.find(name);
  return it != m_LootInfo.end() ? &it->second : nullptr;
}

#pragma endregion ILootCache
#pragma region Slots

void PluginList::writePluginLists() const
{
  const auto gameFeatures = m_Organizer->gameFeatures();
  const auto tesSupport =
      gameFeatures ? gameFeatures->gameFeature<MOBase::GamePlugins>() : nullptr;
  if (tesSupport) {
    tesSupport->writePluginLists(this);
  }

  if (const auto groupsFile = groupsPath(); !groupsFile.isEmpty()) {
    writeGroups(groupsFile);
  }
}

#pragma endregion Slots
#pragma region Helpers

static bool isPluginFile(const QString& filename)
{
  return filename.endsWith(u".esp"_s, Qt::CaseInsensitive) ||
         filename.endsWith(u".esm"_s, Qt::CaseInsensitive) ||
         filename.endsWith(u".esl"_s, Qt::CaseInsensitive);
}

enum class Game
{
  Skyrim,
  Fallout4,
  Starfield,
  Other
};

static bool isAssociatedArchive(TESData::FileInfo& info, const QString& candidate,
                                Game game)
{
  const QString baseName = QFileInfo(info.name()).completeBaseName();
  if (!candidate.startsWith(baseName)) {
    return false;
  }

  switch (game) {
  case Game::Skyrim:
    if (candidate.endsWith(u".bsa"_s)) {
      if (candidate.compare(baseName + u".bsa"_s, Qt::CaseInsensitive) == 0 ||
          candidate.compare(baseName + u" - Textures.bsa"_s, Qt::CaseInsensitive) ==
              0) {
        return true;
      }
    }
    return false;

  case Game::Fallout4:
  case Game::Starfield:
    if (candidate.endsWith(u".ba2"_s)) {
      if (candidate.compare(baseName + u" - Main.ba2"_s, Qt::CaseInsensitive) == 0 ||
          candidate.compare(baseName + u" - Textures.ba2"_s, Qt::CaseInsensitive) ==
              0 ||
          candidate.startsWith(baseName + u" - Voices_"_s, Qt::CaseInsensitive)) {
        return true;
      }
    }
    return false;

  default:
    return candidate.endsWith(u".bsa"_s);
  }
}

void PluginList::checkBsa(TESData::FileInfo& info,
                          const std::shared_ptr<const MOBase::IFileTree>& fileTree)
{
  const auto managedGame = m_Organizer->managedGame();
  const auto gameName    = managedGame ? managedGame->gameName() : QString();
  const Game game        = gameName.startsWith(u"Skyrim"_s)      ? Game::Skyrim
                           : gameName.startsWith(u"Enderal"_s)   ? Game::Skyrim
                           : gameName.startsWith(u"Fallout 4"_s) ? Game::Fallout4
                           : gameName == u"Starfield"_s          ? Game::Starfield
                                                                 : Game::Other;

  const QString baseName = QFileInfo(info.name()).completeBaseName();
  for (const auto entry : *fileTree) {
    if (!entry) {
      continue;
    }

    const auto candidate = entry->name();
    if (isAssociatedArchive(info, candidate, game)) {
      info.addArchive(candidate);
      associateArchive(info, candidate);
    }
  }
}

static void checkIni(TESData::FileInfo& info,
                     const std::shared_ptr<const MOBase::IFileTree>& fileTree)
{
  QString baseName = QFileInfo(info.name()).completeBaseName();
  QString iniPath  = baseName + u".ini"_s;
  if (fileTree->find(iniPath, MOBase::FileTreeEntry::FILE) != nullptr) {
    info.setHasIni(true);
  }
}

static void assignConsecutivePriorities(std::vector<std::shared_ptr<FileInfo>>& plugins)
{
  boost::container::flat_multimap<int, int> priorityToId;
  for (int i = 0; i < plugins.size(); ++i) {
    int priority = plugins[i]->priority();
    if (priority == -1) {
      priority = std::numeric_limits<int>::max();
    }
    priorityToId.emplace(priority, i);
  }

  int newPriority = 0;
  for (auto& [priority, id] : priorityToId) {
    plugins[id]->setPriority(newPriority++);
  }
}

void PluginList::scanDataFiles(bool invalidate)
{
  if (invalidate) {
    m_Plugins.clear();
    m_PluginsByName.clear();
    m_PluginsByPriority.clear();

    m_EntriesByName.clear();
    m_EntriesByHandle.clear();
    m_NextHandle = 0;

    m_MasterArchiveEntry = std::make_shared<AssociatedEntry>();
    m_Archives.clear();
  }

  const auto managedGame  = m_Organizer->managedGame();
  const auto gameFeatures = m_Organizer->gameFeatures();

  const QStringList primaryPlugins =
      managedGame ? managedGame->primaryPlugins() : QStringList();
  const QStringList enabledPlugins =
      managedGame ? managedGame->enabledPlugins() : QStringList();
  const auto loadOrderMechanism = managedGame
                                      ? managedGame->loadOrderMechanism()
                                      : MOBase::IPluginGame::LoadOrderMechanism::None;

  const auto tesSupport =
      gameFeatures ? gameFeatures->gameFeature<MOBase::GamePlugins>() : nullptr;

  const bool lightPluginsAreSupported =
      tesSupport && tesSupport->lightPluginsAreSupported();
  const bool mediumPluginsAreSupported =
      tesSupport && tesSupport->mediumPluginsAreSupported();
  const bool blueprintPluginsAreSupported =
      tesSupport && tesSupport->blueprintPluginsAreSupported();

  QStringList availablePlugins;

  const auto tree = m_Organizer->virtualFileTree();
  for (const std::shared_ptr<const MOBase::FileTreeEntry> entry : *tree) {
    if (entry == nullptr) {
      continue;
    }

    const QString filename = entry->name();

    if (!isPluginFile(filename)) {
      continue;
    }

    if (m_Organizer->resolvePath(filename).isEmpty()) {
      continue;
    }

    availablePlugins.append(filename);
  }

  for (const auto& modName : m_PendingActive) {
    const auto modInterface = m_Organizer->modList()->getMod(modName);
    const auto fileTree     = modInterface ? modInterface->fileTree() : nullptr;

    if (!fileTree)
      continue;

    std::shared_ptr<const MOBase::IFileTree> searchDir;
    if (!m_Organizer->managedGame()->modDataDirectory().isEmpty()) {
      searchDir =
          fileTree->findDirectory(m_Organizer->managedGame()->modDataDirectory());
    } else {
      searchDir = fileTree;
    }
    if (searchDir) {
      for (auto&& entry : *searchDir) {
        if (entry && isPluginFile(entry->name()) &&
            !availablePlugins.contains(entry->name(), Qt::CaseInsensitive)) {
          availablePlugins.append(entry->name());
        }
      }
    }
  }

  // Fluorine port: use ALL cores + force true async (upstream used hw/2 +
  // default launch policy which libstdc++ may run as deferred → sequential).
  const uint concurrency = std::max(1U, std::thread::hardware_concurrency());
  std::counting_semaphore smph{concurrency};

  std::vector<std::shared_future<void>> futures;
  for (const auto& filename : availablePlugins) {
    if (!invalidate && m_PluginsByName.contains(filename)) {
      continue;
    }

    const bool forceLoaded  = primaryPlugins.contains(filename, Qt::CaseInsensitive);
    const bool forceEnabled = enabledPlugins.contains(filename, Qt::CaseInsensitive);
    const bool forceDisabled =
        !forceLoaded && !forceEnabled &&
        (loadOrderMechanism == MOBase::IPluginGame::LoadOrderMechanism::None);

    const QString fullPath = m_Organizer->resolvePath(filename);

    const auto& info = m_Plugins.emplace_back(
        std::make_shared<FileInfo>(this, filename, forceLoaded, forceEnabled,
                                   forceDisabled, lightPluginsAreSupported));

    auto assocTask = std::async(std::launch::async, [this, info, tree, &smph] {
      smph.acquire();
      checkBsa(*info, tree);
      checkIni(*info, tree);
      smph.release();
    });

    auto fileTask = std::async(
        std::launch::async, [=, this, &smph, path = fullPath.toStdString()] {
          smph.acquire();

          try {
            FileConflictParser handler{this, info.get(), lightPluginsAreSupported,
                                       mediumPluginsAreSupported,
                                       blueprintPluginsAreSupported};
            TESFile::Reader<FileConflictParser> reader{};
            // Fluorine port: use UTF-8 bytes (upstream used toStdWString which
            // silently mangles paths through the C locale on Linux). Also fall
            // back to case-insensitive directory lookup — IOrganizer::resolvePath
            // returns paths with the user's original casing, while the file on
            // disk may differ (Windows mods unpacked on case-sensitive Linux).
            std::filesystem::path fspath{path};
            if (!std::filesystem::exists(fspath)) {
              const auto parent   = fspath.parent_path();
              const auto wantName = fspath.filename().string();
              if (std::filesystem::exists(parent)) {
                for (const auto& entry : std::filesystem::directory_iterator{parent}) {
                  const auto name = entry.path().filename().string();
                  if (name.size() == wantName.size() &&
                      std::equal(name.begin(), name.end(), wantName.begin(),
                                 [](unsigned char a, unsigned char b) {
                                   return std::tolower(a) == std::tolower(b);
                                 })) {
                    fspath = entry.path();
                    break;
                  }
                }
              }
            }
            reader.parse(fspath, handler);
          } catch (const std::exception& e) {
            MOBase::log::error("Error parsing \"{}\": {}", path, e.what());
          }

          smph.release();
        });

    futures.push_back(assocTask.share());
    futures.push_back(fileTask.share());
  }

  boost::wait_for_all(futures.begin(), futures.end());

  if (!invalidate) {
    std::erase_if(m_Plugins, [&](auto&& plugin) {
      return !plugin || !availablePlugins.contains(plugin->name(), Qt::CaseInsensitive);
    });
  }

  assignConsecutivePriorities(m_Plugins);
  updateCache();
}

void PluginList::readPluginLists()
{
  const auto gameFeatures = m_Organizer->gameFeatures();
  const auto tesSupport =
      gameFeatures ? gameFeatures->gameFeature<MOBase::GamePlugins>() : nullptr;

  if (tesSupport) {
    tesSupport->readPluginLists(this);
  }

  enforcePluginRelationships();
}

static void populateArchiveFiles(const std::shared_ptr<AuxItem>& master,
                                 const std::shared_ptr<AuxItem>& entry,
                                 const BSA::Folder::Ptr& archiveFolder,
                                 TESFileHandle handle)
{
  for (unsigned int i = 0, num = archiveFolder->getNumFiles(); i < num; ++i) {
    const auto file = archiveFolder->getFile(i);

    const auto conflictItem =
        master->insert(file->getName())->createMember(file->getFilePath());
    conflictItem->alternatives.insert(handle);
    entry->insert(file->getName())->setMember(conflictItem);
  }

  for (unsigned int i = 0, num = archiveFolder->getNumSubFolders(); i < num; ++i) {
    const auto folder = archiveFolder->getSubFolder(i);
    populateArchiveFiles(master->insert(folder->getName()),
                         entry->insert(folder->getName()), folder, handle);
  }
}

void PluginList::associateArchive(const TESData::FileInfo& info,
                                  const QString& archiveName)
{
  const auto archiveEntry = [this, &archiveName] {
    std::unique_lock lk{m_ArchiveEntryMutex};
    auto& entry = m_Archives[archiveName];
    if (entry == nullptr) {
      entry = std::make_shared<AssociatedEntry>();
    }
    return entry;
  }();

  const QString archivePath = m_Organizer->resolvePath(archiveName);
  if (archivePath.isEmpty()) {
    return;
  }

  BSA::Archive archive;
  BSA::EErrorCode result = BSA::ERROR_NONE;

  try {
    result = archive.read(qPrintable(archivePath), false);
  } catch (const std::exception& e) {
    MOBase::log::error("invalid bsa '{}', error {}", archivePath, e.what());
    return;
  }

  if (result != BSA::ERROR_NONE && result != BSA::ERROR_INVALIDHASHES) {
    MOBase::log::error("invalid bsa '{}', error {}", archivePath, result);
    return;
  }

  const auto handle = createEntry(info.name().toStdString())->handle();
  populateArchiveFiles(m_MasterArchiveEntry->root(), archiveEntry->root(),
                       archive.getRoot(), handle);
}

void PluginList::clearGroups()
{
  for (const auto& plugin : m_Plugins) {
    plugin->setGroup(QString());
  }
}

void PluginList::readGroups(const QString& fileName)
{
  clearGroups();

  QFile file{fileName};
  if (!file.exists()) {
    return;
  }

  int lineNumber = 0;
  if (!file.open(QFile::ReadOnly)) {
    return;
  }

  QTextStream stream{&file};

  QString line;
  while (stream.readLineInto(&line)) {
    ++lineNumber;

    if (line.length() <= 0 || line.at(0) == u'#') {
      continue;
    }

    const auto fields = line.split(u'|');
    if (fields.count() != 2) {
      MOBase::log::error("plugin groups file: invalid line #{}: {}", lineNumber, line);
      continue;
    }

    if (const auto it = m_PluginsByName.find(fields[0]); it != m_PluginsByName.end()) {
      const auto& plugin = m_Plugins.at(it->second);
      plugin->setGroup(fields[1]);
    }
  }

  file.close();
}

void PluginList::writeEmptyTextFile(const QString& fileName) const
{
  const QByteArray contents =
      "# This file was automatically generated by Mod Organizer.\r\n"_ba;
  MOBase::TransactionalWriteFile file{fileName};
  if (!file.replaceWith(contents)) {
    MOBase::log::error("failed to save '{}': {}", fileName, file.errorString());
  }
}

void PluginList::writeGroups(const QString& fileName) const
{
  QByteArray contents =
      "# This file was automatically generated by Mod Organizer.\r\n"_ba;
  for (const auto& [name, i] : m_PluginsByName) {
    const auto& plugin = m_Plugins.at(i);
    const auto& group  = plugin->group();
    if (!group.isEmpty()) {
      contents.append(u"%1|%2\r\n"_s.arg(name).arg(group).toUtf8());
    }
  }

  MOBase::TransactionalWriteFile file{fileName};
  if (!file.replaceWith(contents)) {
    MOBase::log::error("failed to save '{}': {}", fileName, file.errorString());
  }
}

void PluginList::queuePluginStateChange(const QString& pluginName, PluginStates state)
{
  if (m_Refreshing) {
    m_QueuedStateChanges[pluginName] = state;
  } else {
    computeCompileIndices();
    refreshLoadOrder();
    pluginStatesChanged({pluginName}, state);
    testMasters();
  }
}

void PluginList::dispatchPluginStateChanges()
{
  if (!m_QueuedStateChanges.empty()) {
    m_PluginStateChanged(m_QueuedStateChanges);
    m_QueuedStateChanges.clear();
  }
}

void PluginList::pluginStatesChanged(const QStringList& pluginNames,
                                     PluginStates state) const
{
  if (pluginNames.isEmpty()) {
    return;
  }

  std::map<QString, IPluginList::PluginStates> infos;
  for (const auto& name : pluginNames) {
    infos[name] = state;
  }

  m_PluginStateChanged(infos);
}

void PluginList::enforcePluginRelationships()
{
  MOBase::TimeThis tt{"TESData::PluginList::enforcePluginRelationships"};

  for (int i = 0; i < m_PluginsByPriority.size(); ++i) {
    const int& firstIndex = m_PluginsByPriority[i];

    for (int j = i + 1; j < m_PluginsByPriority.size(); ++j) {
      const int secondIndex    = m_PluginsByPriority[j];
      const auto& firstPlugin  = m_Plugins.at(firstIndex);
      const auto& secondPlugin = m_Plugins.at(secondIndex);

      if (firstPlugin->mustLoadAfter(*secondPlugin)) {
        for (int k = j; k > i + 1; --k) {
          m_PluginsByPriority[k] = m_PluginsByPriority[k - 1];
          m_Plugins[m_PluginsByPriority[k]]->setPriority(k);
        }
        m_PluginsByPriority[i + 1] = firstIndex;
        firstPlugin->setPriority(i + 1);
        m_PluginsByPriority[i] = secondIndex;
        secondPlugin->setPriority(i);
        j = i;
      }
    }
  }

  computeCompileIndices();
  refreshLoadOrder();
}

void PluginList::testMasters()
{
  boost::container::flat_set<QString, MOBase::FileNameComparator> enabledMasters;
  for (const auto& plugin : m_Plugins) {
    if (plugin->enabled()) {
      enabledMasters.insert(plugin->name());
    }
  }

  for (auto& plugin : m_Plugins) {
    std::vector<QString> missingMasters;
    std::ranges::remove_copy_if(plugin->masters(), std::back_inserter(missingMasters),
                                [&](auto&& file) {
                                  return enabledMasters.contains(file);
                                });
    plugin->setMissingMasters(missingMasters);
  }
}

void PluginList::updateCache()
{
  m_PluginsByName.clear();
  m_PluginsByPriority.clear();
  m_PluginsByPriority.resize(m_Plugins.size());
  for (int i = 0; i < m_Plugins.size(); ++i) {
    if (m_Plugins[i]->priority() < 0) {
      continue;
    }
    if (m_Plugins[i]->priority() >= static_cast<int>(m_Plugins.size())) {
      MOBase::log::error("invalid plugin priority: {}", m_Plugins[i]->priority());
      continue;
    }
    m_PluginsByName[m_Plugins[i]->name()]         = i;
    m_PluginsByPriority[m_Plugins[i]->priority()] = i;
  }

  computeCompileIndices();
  refreshLoadOrder();
}

void PluginList::computeCompileIndices()
{
  int numNormal  = 0;
  int numESLs    = 0;
  int numESHs    = 0;
  int numSkipped = 0;

  const auto gameFeatures = m_Organizer->gameFeatures();
  const auto tesSupport =
      gameFeatures ? gameFeatures->gameFeature<MOBase::GamePlugins>() : nullptr;

  const bool lightPluginsAreSupported =
      tesSupport && tesSupport->lightPluginsAreSupported();
  const bool mediumPluginsAreSupported =
      tesSupport && tesSupport->mediumPluginsAreSupported();

  for (int priority = 0; priority < m_PluginsByPriority.size(); ++priority) {
    const int index    = m_PluginsByPriority[priority];
    const auto& plugin = m_Plugins.at(index);

    if (!plugin->enabled()) {
      plugin->setIndex(QString());
      ++numSkipped;
      continue;
    }

    if (mediumPluginsAreSupported && plugin->isMediumFile()) {
      const int ESHpos = 0xFD + (numESHs >> 8);
      plugin->setIndex((u"%1:%2"_s)
                           .arg(ESHpos, 2, 16, QChar(u'0'))
                           .arg(numESHs & 0xFF, 2, 16, QChar(u'0'))
                           .toUpper());
    } else if (lightPluginsAreSupported && plugin->isSmallFile()) {
      const int ESLpos = 0xFE + (numESLs >> 12);
      plugin->setIndex((u"%1:%2"_s)
                           .arg(ESLpos, 2, 16, QChar(u'0'))
                           .arg(numESLs & 0xFFF, 3, 16, QChar(u'0'))
                           .toUpper());
      ++numESLs;
    } else {
      plugin->setIndex((u"%1"_s).arg(numNormal++, 2, 16, QChar(u'0')).toUpper());
    }
  }
}

void PluginList::refreshLoadOrder()
{
  int loadOrder = 0;
  for (int i = 0; i < m_PluginsByPriority.size(); ++i) {
    int index          = m_PluginsByPriority[i];
    const auto& plugin = m_Plugins.at(index);

    if (plugin->enabled()) {
      plugin->setLoadOrder(loadOrder++);
    } else {
      plugin->setLoadOrder(-1);
    }
  }
  emit pluginsListChanged();
}

#pragma endregion Helpers

}  // namespace TESData

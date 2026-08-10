/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "categories.h"
#include "categoryassignmentpolicy.h"
#include "categoryfileparser.h"
#include "categorypersistence.h"
#include "settings.h"

#include <log.h>
#include <report.h>
#include <utility.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QList>
#include <QObject>

#include "nexusinterface.h"

using namespace MOBase;

namespace
{
int localCategoryHighWater(
    const std::vector<CategoryFactory::Category>& categories)
{
  int highWater = 0;
  for (const auto& category : categories) {
    if (category.ID() > highWater &&
        category.ID() < CategoryAssignmentPolicy::ImportedCategoryIdBase) {
      highWater = category.ID();
    }
  }
  return highWater;
}

bool reserveLocalCategoryIds(
    const std::vector<CategoryFactory::Category>& categories)
{
  auto* settings = Settings::maybeInstance();
  return settings == nullptr || settings->advanceCategoryLocalIdHighWater(
                                    localCategoryHighWater(categories));
}
}  // namespace

CategoryFactory* CategoryFactory::s_Instance = nullptr;

QString CategoryFactory::categoriesFilePath()
{
  return qApp->property("dataPath").toString() + "/categories.dat";
}

CategoryFactory::CategoryFactory()
{
  atexit(&cleanup);
  reset();
  m_CategoriesLoaded = false;
}

QString CategoryFactory::nexusMappingFilePath()
{
  return qApp->property("dataPath").toString() + "/nexuscatmap.dat";
}

bool CategoryFactory::resetCategoryStorage(QStringList* backupPaths)
{
  return CategoryPersistence::resetFiles(
      categoriesFilePath(), nexusMappingFilePath(), backupPaths);
}

bool CategoryFactory::loadCategories(bool allowCreate)
{
  m_CategoriesLoaded = false;
  m_StorageVersion.clear();
  QString quarantinedJournal;
  const auto files = CategoryPersistence::readFiles(
      categoriesFilePath(), nexusMappingFilePath(), &quarantinedJournal);
  if (!files) {
    m_CategoriesLoaded = false;
    if (!quarantinedJournal.isEmpty()) {
      reportError(
          tr("A damaged category transaction was preserved at %1. Category "
             "data will remain unavailable until the preserved transaction "
             "and category files are explicitly repaired or reset.")
              .arg(quarantinedJournal));
    } else {
      reportError(tr("Failed to read or recover the category files"));
    }
    return false;
  }

  const auto& [categorySnapshot, nexusMapSnapshot] = *files;
  const QByteArray loadedVersion = CategoryPersistence::storageVersion(
      categorySnapshot, nexusMapSnapshot);
  if (loadedVersion.isEmpty()) {
    reportError(tr("Failed to identify the loaded category files"));
    return false;
  }
  if (categorySnapshot.existed != nexusMapSnapshot.existed) {
    m_CategoriesLoaded = false;
    reportError(tr("The category and Nexus mapping files are incomplete; both "
                   "files must be repaired or reset together"));
    return false;
  }

  if (!categorySnapshot.existed) {
    if (!allowCreate) {
      m_CategoriesLoaded = false;
      reportError(tr("The category files are missing from an established "
                     "instance; use category recovery to reset them"));
      return false;
    }

    reset();
    loadDefaultCategories();
    m_CategoriesLoaded = true;
    m_StorageVersion   = loadedVersion;
    if (!saveCategories()) {
      m_CategoriesLoaded = false;
      return false;
    }
    return true;
  }

  const auto parsed = CategoryFileParser::parse(categorySnapshot.data,
                                                nexusMapSnapshot.data);
  if (!parsed) {
    m_CategoriesLoaded = false;
    reportError(tr("The category files are malformed: %1").arg(parsed.error));
    return false;
  }

  reset();
  m_CategoriesLoaded = false;
  for (const auto& category : parsed.inventory->categories) {
    std::vector<NexusCategory> nexusCategories;
    nexusCategories.reserve(category.nexusIds.size());
    for (const int nexusId : category.nexusIds) {
      nexusCategories.emplace_back(QStringLiteral("Unknown"), nexusId);
    }
    addCategory(category.id, category.name, nexusCategories,
                category.parentId);
  }
  for (const auto& mapping : parsed.inventory->nexusMappings) {
    m_NexusMap.insert_or_assign(
        mapping.nexusId, NexusCategory(mapping.name, mapping.nexusId));
    m_NexusMap.at(mapping.nexusId).setCategoryID(mapping.categoryId);
  }
  std::sort(m_Categories.begin(), m_Categories.end());
  setParents();
  m_CategoriesLoaded = true;
  m_StorageVersion   = loadedVersion;
  if (!reserveLocalCategoryIds(m_Categories)) {
    m_CategoriesLoaded = false;
    reportError(tr("Failed to reserve local category identifiers"));
    return false;
  }
  return true;
}

CategoryFactory& CategoryFactory::instance()
{
  static CategoryFactory s_Instance;
  return s_Instance;
}

void CategoryFactory::reset()
{
  m_Categories.clear();
  m_NexusMap.clear();
  m_IDMap.clear();
  addCategory(0, "None", std::vector<NexusCategory>(), 0);
}

void CategoryFactory::setParents()
{
  for (auto& category : m_Categories) {
    category.setHasChildren(false);
  }

  for (const auto& category : m_Categories) {
    if (category.parentID() != 0) {
      std::map<int, unsigned int>::const_iterator const iter =
          m_IDMap.find(category.parentID());
      if (iter != m_IDMap.end()) {
        m_Categories[iter->second].setHasChildren(true);
      }
    }
  }
}

void CategoryFactory::cleanup()
{
  delete s_Instance;
  s_Instance = nullptr;
}

bool CategoryFactory::saveCategories()
{
  if (!m_CategoriesLoaded) {
    reportError(tr("Categories are unavailable because recovery did not "
                   "complete; reload them before saving"));
    return false;
  }
  if (!reserveLocalCategoryIds(m_Categories)) {
    reportError(tr("Failed to reserve local category identifiers"));
    return false;
  }

  QByteArray categoriesData;
  for (const auto& category : m_Categories) {
    if (category.ID() == 0) {
      continue;
    }
    categoriesData.append(QByteArray::number(category.ID()))
        .append("|")
        .append(category.name().toUtf8())
        .append("|")
        .append(QByteArray::number(category.parentID()))
        .append("\n");
  }

  QByteArray nexusMapData;
  for (const auto& nexMap : m_NexusMap) {
    nexusMapData.append(QByteArray::number(nexMap.second.categoryID())).append("|");
    nexusMapData.append(nexMap.second.name().toUtf8()).append("|");
    nexusMapData.append(QByteArray::number(nexMap.second.ID())).append("\n");
  }

  const auto validation =
      CategoryFileParser::parse(categoriesData, nexusMapData);
  if (!validation) {
    reportError(tr("Cannot save invalid category data: %1")
                    .arg(validation.error));
    return false;
  }

  if (m_StorageVersion.isEmpty()) {
    reportError(tr("Cannot save categories without a loaded storage generation"));
    return false;
  }

  const QByteArray newStorageVersion = CategoryPersistence::storageVersion(
      {true, categoriesData}, {true, nexusMapData});
  if (newStorageVersion.isEmpty()) {
    reportError(tr("Failed to identify the new category files"));
    return false;
  }

  const auto result = CategoryPersistence::writeFiles(
      categoriesFilePath(), categoriesData, nexusMappingFilePath(), nexusMapData,
      m_StorageVersion);
  if (result == CategoryPersistence::WriteResult::Conflict) {
    reportError(tr("Categories changed in another process; reload the instance "
                   "before saving category edits"));
    return false;
  }
  if (result == CategoryPersistence::WriteResult::CategoriesFailed) {
    reportError(tr("Failed to save custom categories"));
    return false;
  }
  if (result == CategoryPersistence::WriteResult::NexusMapFailed) {
    reportError(tr("Failed to save nexus category mappings"));
    return false;
  }

  m_StorageVersion = newStorageVersion;
  ++m_SaveGeneration;
  emit categoriesSaved();
  return true;
}

bool CategoryFactory::replaceCategoriesFromNexus(
    const std::vector<NexusCategory>& nexusCats,
    quint64 expectedSaveGeneration)
{
  if (m_SaveGeneration != expectedSaveGeneration) {
    reportError(tr("Categories changed while the Nexus inventory was being "
                   "downloaded; the import was cancelled"));
    return false;
  }
  if (nexusCats.empty()) {
    reportError(tr("The imported category inventory is empty"));
    return false;
  }
  if (!reserveLocalCategoryIds(m_Categories)) {
    reportError(tr("Failed to reserve retired local category identifiers"));
    return false;
  }

  std::vector<std::pair<int, NexusCategory>> importedCategories;
  importedCategories.reserve(nexusCats.size());
  std::set<int> seenNexusIds;
  for (const auto& nexusCategory : nexusCats) {
    const auto localId = CategoryAssignmentPolicy::importedCategoryId(
        nexusCategory.ID());
    if (!localId ||
        !CategoryAssignmentPolicy::isSafeSerializedName(
            nexusCategory.name()) ||
        !seenNexusIds.insert(nexusCategory.ID()).second) {
      reportError(tr("The imported category inventory contains invalid data"));
      return false;
    }

    // A manually assigned local ID in the imported namespace is not safe to
    // reinterpret. Reuse is allowed only when the existing Nexus mapping
    // proves that the ID already has the same stable identity.
    if (m_IDMap.contains(*localId)) {
      const auto existing = m_NexusMap.find(nexusCategory.ID());
      if (existing == m_NexusMap.end() ||
          existing->second.categoryID() != *localId) {
        reportError(tr("Imported category ID %1 conflicts with an existing "
                       "custom category")
                        .arg(*localId));
        return false;
      }
    }
    importedCategories.emplace_back(*localId, nexusCategory);
  }

  const auto previousCategories = m_Categories;
  const auto previousNexusMap   = m_NexusMap;
  const auto previousIDMap      = m_IDMap;
  const bool previousLoaded     = m_CategoriesLoaded;

  reset();
  for (const auto& [localId, nexusCategory] : importedCategories) {
    addCategory(localId, nexusCategory.name(), {nexusCategory}, 0);
  }

  if (saveCategories()) {
    return true;
  }

  m_Categories = previousCategories;
  m_NexusMap   = previousNexusMap;
  m_IDMap      = previousIDMap;
  m_CategoriesLoaded = previousLoaded;
  return false;
}

unsigned int
CategoryFactory::countCategories(std::function<bool(const Category& category)> filter)
{
  unsigned int result = 0;
  for (const Category& cat : m_Categories) {
    if (filter(cat)) {
      ++result;
    }
  }
  return result;
}

int CategoryFactory::addCategory(const QString& name,
                                 const std::vector<NexusCategory>& nexusCats,
                                 int parentID)
{
  const auto previousCategories = m_Categories;
  const auto previousNexusMap   = m_NexusMap;
  const auto previousIDMap      = m_IDMap;
  std::set<int> usedIds;
  for (const auto& category : m_Categories) {
    usedIds.insert(category.ID());
  }
  const auto id = CategoryAssignmentPolicy::nextLocalCategoryId(
      Settings::instance().categoryLocalIdHighWater(), usedIds);
  if (!id || !Settings::instance().advanceCategoryLocalIdHighWater(*id)) {
    reportError(tr("Failed to reserve a new local category identifier"));
    return -1;
  }
  addCategory(*id, name, nexusCats, parentID);

  if (saveCategories()) {
    return *id;
  }
  m_Categories = previousCategories;
  m_NexusMap   = previousNexusMap;
  m_IDMap      = previousIDMap;
  return -1;
}

void CategoryFactory::addCategory(int id, const QString& name, int parentID)
{
  int const index = static_cast<int>(m_Categories.size());
  m_Categories.emplace_back(index, id, name, parentID, std::vector<NexusCategory>());
  m_IDMap[id] = index;
}

void CategoryFactory::addCategory(int id, const QString& name,
                                  const std::vector<NexusCategory>& nexusCats,
                                  int parentID)
{
  for (const auto& nexusCat : nexusCats) {
    m_NexusMap.insert_or_assign(nexusCat.ID(), nexusCat);
    m_NexusMap.at(nexusCat.ID()).setCategoryID(id);
  }
  int const index = static_cast<int>(m_Categories.size());
  m_Categories.emplace_back(index, id, name, parentID, nexusCats);
  m_IDMap[id] = index;
}

void CategoryFactory::setNexusCategories(
    const std::vector<CategoryFactory::NexusCategory>& nexusCats)
{
  for (const auto& nexusCat : nexusCats) {
    m_NexusMap.emplace(nexusCat.ID(), nexusCat);
  }
}

void CategoryFactory::refreshNexusCategories(CategoriesDialog* dialog)
{
  emit nexusCategoryRefresh(dialog);
}

void CategoryFactory::loadDefaultCategories()
{
  // the order here is relevant as it defines the order in which the
  // mods appear in the combo box
  addCategory(1, "Animations", 0);
  addCategory(52, "Poses", 1);
  addCategory(2, "Armour", 0);
  addCategory(53, "Power Armor", 2);
  addCategory(3, "Audio", 0);
  addCategory(38, "Music", 0);
  addCategory(59, "Voice", 0);
  addCategory(5, "Clothing", 0);
  addCategory(41, "Jewelry", 5);
  addCategory(42, "Backpacks", 5);
  addCategory(6, "Collectables", 0);
  addCategory(28, "Companions", 0);
  addCategory(7, "Creatures, Mounts, & Vehicles", 0);
  addCategory(8, "Factions", 0);
  addCategory(9, "Gameplay", 0);
  addCategory(27, "Combat", 9);
  addCategory(43, "Crafting", 9);
  addCategory(48, "Overhauls", 9);
  addCategory(49, "Perks", 9);
  addCategory(54, "Radio", 9);
  addCategory(55, "Shouts", 9);
  addCategory(22, "Skills & Levelling", 9);
  addCategory(58, "Weather & Lighting", 9);
  addCategory(44, "Equipment", 43);
  addCategory(45, "Home/Settlement", 43);
  addCategory(10, "Body, Face, & Hair", 0);
  addCategory(39, "Tattoos", 10);
  addCategory(40, "Character Presets", 0);
  addCategory(11, "Items", 0);
  addCategory(32, "Mercantile", 0);
  addCategory(37, "Ammo", 11);
  addCategory(19, "Weapons", 11);
  addCategory(36, "Weapon & Armour Sets", 11);
  addCategory(23, "Player Homes", 0);
  addCategory(25, "Castles & Mansions", 23);
  addCategory(51, "Settlements", 23);
  addCategory(12, "Locations", 0);
  addCategory(4, "Cities", 12);
  addCategory(31, "Landscape Changes", 0);
  addCategory(29, "Environment", 0);
  addCategory(30, "Immersion", 0);
  addCategory(20, "Magic", 0);
  addCategory(21, "Models & Textures", 0);
  addCategory(33, "Modders resources", 0);
  addCategory(13, "NPCs", 0);
  addCategory(24, "Bugfixes", 0);
  addCategory(14, "Patches", 24);
  addCategory(35, "Utilities", 0);
  addCategory(26, "Cheats", 0);
  addCategory(15, "Quests", 0);
  addCategory(16, "Races & Classes", 0);
  addCategory(34, "Stealth", 0);
  addCategory(17, "UI", 0);
  addCategory(18, "Visuals", 0);
  addCategory(50, "Pip-Boy", 18);
  addCategory(46, "Shader Presets", 0);
  addCategory(47, "Miscellaneous", 0);
}

int CategoryFactory::getParentID(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].parentID();
}

bool CategoryFactory::categoryExists(int id) const
{
  return m_IDMap.contains(id);
}

bool CategoryFactory::isDescendantOf(int id, int parentID) const
{
  // handles cycles
  std::set<int> seen;
  return isDescendantOfImpl(id, parentID, seen);
}

bool CategoryFactory::isDescendantOfImpl(int id, int parentID,
                                         std::set<int>& seen) const
{
  if (!seen.insert(id).second) {
    log::error("cycle in category: {}", id);
    return false;
  }

  std::map<int, unsigned int>::const_iterator const iter = m_IDMap.find(id);

  if (iter != m_IDMap.end()) {
    unsigned int const index = iter->second;
    if (m_Categories[index].parentID() == 0) {
      return false;
    } else if (m_Categories[index].parentID() == parentID) {
      return true;
    } else {
      return isDescendantOfImpl(m_Categories[index].parentID(), parentID, seen);
    }
  } else {
    log::warn(tr("{} is no valid category id"), id);
    return false;
  }
}

bool CategoryFactory::hasChildren(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].hasChildren();
}

QString CategoryFactory::getCategoryName(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].name();
}

QString CategoryFactory::getSpecialCategoryName(SpecialCategories type)
{
  QString label;
  switch (type) {
  case Checked:
    label = QObject::tr("Active");
    break;
  case UpdateAvailable:
    label = QObject::tr("Update available");
    break;
  case HasCategory:
    label = QObject::tr("Has category");
    break;
  case Conflict:
    label = QObject::tr("Conflicted");
    break;
  case HasHiddenFiles:
    label = QObject::tr("Has hidden files");
    break;
  case Endorsed:
    label = QObject::tr("Endorsed");
    break;
  case Backup:
    label = QObject::tr("Has backup");
    break;
  case Managed:
    label = QObject::tr("Managed");
    break;
  case HasGameData:
    label = QObject::tr("Has valid game data");
    break;
  case HasNexusID:
    label = QObject::tr("Has Nexus ID");
    break;
  case Tracked:
    label = QObject::tr("Tracked on Nexus");
    break;
  default:
    return {};
  }
  return QString("<%1>").arg(label);
}

QString CategoryFactory::getCategoryNameByID(int id) const
{
  auto itor = m_IDMap.find(id);

  if (itor == m_IDMap.end()) {
    return getSpecialCategoryName(static_cast<SpecialCategories>(id));
  } else {
    const auto index = itor->second;
    if (index >= m_Categories.size()) {
      return {};
    }

    return m_Categories[index].name();
  }
}

int CategoryFactory::getCategoryID(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].ID();
}

int CategoryFactory::getCategoryIndex(int ID) const
{
  std::map<int, unsigned int>::const_iterator const iter = m_IDMap.find(ID);
  if (iter == m_IDMap.end()) {
    throw MyException(tr("invalid category id: %1").arg(ID));
  }
  return iter->second;
}

int CategoryFactory::getCategoryID(const QString& name) const
{
  auto iter = std::find_if(m_Categories.begin(), m_Categories.end(),
                           [name](const Category& cat) -> bool {
                             return cat.name() == name;
                           });

  if (iter != m_Categories.end()) {
    return iter->ID();
  } else {
    return -1;
  }
}

unsigned int CategoryFactory::resolveNexusID(int nexusID) const
{
  auto result = m_NexusMap.find(nexusID);
  if (result != m_NexusMap.end()) {
    if (m_IDMap.contains(result->second.categoryID())) {
      log::debug(tr("nexus category id {0} maps to internal {1}"), nexusID,
                 m_IDMap.at(result->second.categoryID()));
      return m_IDMap.at(result->second.categoryID());
    }
  }
  log::debug(tr("nexus category id {} not mapped"), nexusID);
  return 0U;
}

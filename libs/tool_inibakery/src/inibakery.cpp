#include "inibakery.h"

#include <functional>

#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QtPlugin>

#include <uibase/game_features/bsainvalidation.h>
#include <uibase/game_features/igamefeatures.h>
#include <uibase/game_features/localsavegames.h>
#include <uibase/iplugingame.h>

using namespace MOBase;

bool IniBakery::init(MOBase::IOrganizer* moInfo)
{
  using namespace std::placeholders;
  m_MOInfo = moInfo;
  m_MOInfo->onAboutToRun([this](const auto& binary) {
    return prepareIni(binary);
  });

  return true;
}

QString IniBakery::name() const
{
  return "INI Bakery";
}

QString IniBakery::localizedName() const
{
  return tr("INI Bakery");
}

QString IniBakery::author() const
{
  return "Tannin";
}

QString IniBakery::description() const
{
  return tr("Manages game ini files");
}

MOBase::VersionInfo IniBakery::version() const
{
  return VersionInfo(0, 3, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> IniBakery::settings() const
{
  return QList<PluginSetting>();
}

QStringList IniBakery::iniFileNames() const
{
  return m_MOInfo->managedGame()->iniFiles();
}

bool IniBakery::prepareIni(const QString&)
{
  const auto* features = m_MOInfo->gameFeatures();

  auto savegames = features->gameFeature<MOBase::LocalSavegames>();
  if (savegames != nullptr) {
    savegames->prepareProfile(m_MOInfo->profile().get());
  }

  auto invalidation = features->gameFeature<BSAInvalidation>();
  if (invalidation != nullptr) {
    invalidation->prepareProfile(m_MOInfo->profile().get());
  }

  return true;
}

MappingType IniBakery::mappings() const
{
  MappingType result;

  const IPluginGame* game = m_MOInfo->managedGame();

  auto profile = m_MOInfo->profile();

  if (profile->localSettingsEnabled()) {
    for (const QString& iniFile : iniFileNames()) {
      result.push_back({m_MOInfo->profilePath() + "/" + QFileInfo(iniFile).fileName(),
                        game->documentsDirectory().absoluteFilePath(iniFile), false,
                        false});
    }
  }

  return result;
}

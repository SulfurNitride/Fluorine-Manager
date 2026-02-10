#include "morrowindgameplugins.h"
#include "registry.h"
#include <imodinterface.h>
#include <iplugingame.h>
#include <ipluginlist.h>
#include <report.h>
#include <scopeguard.h>
#include <utility.h>

#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QString>
#include <QStringEncoder>
#include <QStringList>

using MOBase::IOrganizer;
using MOBase::IPluginList;
using MOBase::reportError;

MorrowindGamePlugins::MorrowindGamePlugins(IOrganizer* organizer)
    : GamebryoGamePlugins(organizer)
{}

void MorrowindGamePlugins::writePluginLists(const IPluginList* pluginList)
{
  if (!m_LastRead.isValid()) {
    // attempt to write uninitialized plugin lists
    return;
  }

  if (organizer()->profile()->localSettingsEnabled()) {
    writePluginList(pluginList,
                    organizer()->profile()->absolutePath() + "/Morrowind.ini");
  } else {
    writePluginList(pluginList,
                    organizer()->managedGame()->gameDirectory().absolutePath() +
                        "/Morrowind.ini");
  }

  writeLoadOrderList(pluginList,
                     organizer()->profile()->absolutePath() + "/loadorder.txt");

  m_LastRead = QDateTime::currentDateTime();
}

void MorrowindGamePlugins::readPluginLists(MOBase::IPluginList* pluginList)
{
  QString loadOrderPath = organizer()->profile()->absolutePath() + "/loadorder.txt";

  QString pluginsPath = organizer()->profile()->absolutePath() + "/Morrowind.ini";
  if (!organizer()->profile()->localSettingsEnabled()) {
    pluginsPath =
        organizer()->managedGame()->gameDirectory().absolutePath() + "/Morrowind.ini";
  }

  bool loadOrderIsNew = !m_LastRead.isValid() || !QFileInfo(loadOrderPath).exists() ||
                        QFileInfo(loadOrderPath).lastModified() > m_LastRead;
  bool pluginsIsNew =
      !m_LastRead.isValid() || QFileInfo(pluginsPath).lastModified() > m_LastRead;

  if (loadOrderIsNew || !pluginsIsNew) {
    // read both files if they are both new or both older than the last read
    QStringList loadOrder = readLoadOrderList(pluginList, loadOrderPath);
    pluginList->setLoadOrder(loadOrder);
    readPluginList(pluginList);
  } else {
    // If the plugins is new but not loadorder, we must reparse the load order from the
    // plugin files
    QStringList loadOrder = readPluginList(pluginList);
    pluginList->setLoadOrder(loadOrder);
  }

  m_LastRead = QDateTime::currentDateTime();
}

void MorrowindGamePlugins::writePluginList(const MOBase::IPluginList* pluginList,
                                           const QString& filePath)
{
  return writeList(pluginList, filePath, false);
}

void MorrowindGamePlugins::writeList(const IPluginList* pluginList,
                                     const QString& filePath, bool loadOrder)
{
  QStringEncoder encoder = loadOrder
                               ? QStringEncoder(QStringConverter::Encoding::Utf8)
                               : QStringEncoder(QStringConverter::Encoding::System);

  QSettings settings(filePath, QSettings::IniFormat);
  settings.remove("Game Files");

  bool invalidFileNames = false;
  int writtenCount      = 0;

  QStringList plugins = pluginList->pluginNames();
  std::sort(plugins.begin(), plugins.end(),
            [pluginList](const QString& lhs, const QString& rhs) {
              return pluginList->priority(lhs) < pluginList->priority(rhs);
            });
  QString key = "GameFile";
  for (const QString& pluginName : plugins) {
    if (loadOrder || (pluginList->state(pluginName) == IPluginList::STATE_ACTIVE)) {
      auto result = encoder.encode(pluginName);
      if (encoder.hasError()) {
        invalidFileNames = true;
        qCritical("invalid plugin name %s", qUtf8Printable(pluginName));
      } else {
        if (!MOBase::WriteRegistryValue(
                "Game Files", key + QString::number(writtenCount), pluginName,
                filePath)) {
          qWarning("failed to set game files in \"%s\"", qUtf8Printable(filePath));
        }
      }
      ++writtenCount;
    }
  }

  if (invalidFileNames) {
    reportError(QObject::tr("Some of your plugins have invalid names! These "
                            "plugins can not be loaded by the game. Please see "
                            "mo_interface.log for a list of affected plugins "
                            "and rename them."));
  }

  if (writtenCount == 0) {
    qWarning("plugin list would be empty, this is almost certainly wrong. Not "
             "saving.");
  }
}

QStringList MorrowindGamePlugins::readPluginList(MOBase::IPluginList* pluginList)
{
  QStringList primary = organizer()->managedGame()->primaryPlugins();
  for (const QString& pluginName : primary) {
    if (pluginList->state(pluginName) != IPluginList::STATE_MISSING) {
      pluginList->setState(pluginName, IPluginList::STATE_ACTIVE);
    }
  }
  QStringList plugins = pluginList->pluginNames();
  // Do not sort the primary plugins. Their load order should be locked as defined in
  // "primaryPlugins".
  const QStringList pluginsClone(plugins);
  for (QString plugin : pluginsClone) {
    if (primary.contains(plugin, Qt::CaseInsensitive))
      plugins.removeAll(plugin);
  }

  // Always use filetime loadorder to get the actual load order
  std::sort(plugins.begin(), plugins.end(),
            [&](const QString& lhs, const QString& rhs) {
              MOBase::IModInterface* lhm =
                  organizer()->modList()->getMod(pluginList->origin(lhs));
              MOBase::IModInterface* rhm =
                  organizer()->modList()->getMod(pluginList->origin(rhs));
              QDir lhd = organizer()->managedGame()->dataDirectory();
              QDir rhd = organizer()->managedGame()->dataDirectory();
              if (lhm != nullptr)
                lhd = lhm->absolutePath();
              if (rhm != nullptr)
                rhd = rhm->absolutePath();
              QString lhp = lhd.absoluteFilePath(lhs);
              QString rhp = rhd.absoluteFilePath(rhs);
              return QFileInfo(lhp).lastModified() < QFileInfo(rhp).lastModified();
            });

  QString filePath = organizer()->profile()->absolutePath() + "/Morrowind.ini";
  if (!organizer()->profile()->localSettingsEnabled()) {
    filePath =
        organizer()->managedGame()->gameDirectory().absolutePath() + "/Morrowind.ini";
  }
  QStringList activePlugins;
  QStringList inactivePlugins;
  QSettings settings(filePath, QSettings::IniFormat);
  QString key = "GameFile";
  int i       = 0;
  while (true) {
    QString pluginName =
        settings.value("Game Files/" + key + QString::number(i), "").toString().trimmed();
    if (pluginName.isEmpty()) {
      break;
    }
    pluginList->setState(pluginName, IPluginList::STATE_ACTIVE);
    activePlugins.push_back(pluginName);
    i++;
  }

  // we removed each plugin found in the file, so what's left are inactive mods
  for (const QString& pluginName : plugins)
    if (!activePlugins.contains(pluginName))
      inactivePlugins.push_back(pluginName);

  for (const QString& pluginName : inactivePlugins)
    pluginList->setState(pluginName, IPluginList::STATE_INACTIVE);

  return primary + plugins;
}

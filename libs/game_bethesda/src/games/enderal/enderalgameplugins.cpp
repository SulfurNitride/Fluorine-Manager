#include "enderalgameplugins.h"
#include <ipluginlist.h>
#include <report.h>
#include <safewritefile.h>
#include <scopeguard.h>

#include <QDir>
#include <QStringEncoder>
#include <QStringList>

using MOBase::IOrganizer;
using MOBase::IPluginGame;
using MOBase::IPluginList;
using MOBase::reportError;
using MOBase::SafeWriteFile;

EnderalGamePlugins::EnderalGamePlugins(IOrganizer* organizer)
    : GamebryoGamePlugins(organizer)
{}

void EnderalGamePlugins::readPluginLists(MOBase::IPluginList* pluginList)
{
  QString loadOrderPath = organizer()->profile()->absolutePath() + "/loadorder.txt";
  QString pluginsPath   = organizer()->profile()->absolutePath() + "/plugins.txt";

  bool loadOrderIsNew = !m_LastRead.isValid() || !QFileInfo(loadOrderPath).exists() ||
                        QFileInfo(loadOrderPath).lastModified() > m_LastRead;
  bool pluginsIsNew =
      !m_LastRead.isValid() || QFileInfo(pluginsPath).lastModified() > m_LastRead;

  if (pluginsIsNew && !loadOrderIsNew) {
    // If the plugins is new but not loadorder, we must reparse the load order from the
    // plugin files

    // removed because returned loadorder was incorrect and did not account for plugins
    // that were already disabled before.
    /*QStringList loadOrder = readPluginList(pluginList);
    pluginList->setLoadOrder(loadOrder);*/

    // Fix me: we are ignoring order changes in plugins.txt favouring loadorder.txt in
    // all cases (plugins.txt should have precedence)
    QStringList loadOrder = readLoadOrderList(pluginList, loadOrderPath);
    pluginList->setLoadOrder(loadOrder);
    readPluginList(pluginList);
  } else {
    // read both files if they are both new or both older than the last read
    QStringList loadOrder = readLoadOrderList(pluginList, loadOrderPath);
    pluginList->setLoadOrder(loadOrder);
    readPluginList(pluginList);
  }

  m_LastRead = QDateTime::currentDateTime();
}

// TODO: return value is incorrect and should be ignored (it's not currently used
QStringList EnderalGamePlugins::readPluginList(MOBase::IPluginList* pluginList)
{
  QStringList plugins        = pluginList->pluginNames();
  QStringList primaryPlugins = organizer()->managedGame()->primaryPlugins();
  QStringList loadOrder(plugins);

  for (const QString& pluginName : primaryPlugins) {
    if (pluginList->state(pluginName) != IPluginList::STATE_MISSING) {
      pluginList->setState(pluginName, IPluginList::STATE_ACTIVE);
    }
  }

  // Do not sort the primary plugins. Their load order should be locked as defined in
  // "primaryPlugins".
  const QStringList pluginsClone(plugins);
  for (QString plugin : pluginsClone) {
    if (primaryPlugins.contains(plugin, Qt::CaseInsensitive))
      plugins.removeAll(plugin);
  }

  // Determine plugin active state by the plugins.txt file.
  bool pluginsTxtExists = true;
  QString filePath      = organizer()->profile()->absolutePath() + "/plugins.txt";
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    pluginsTxtExists = false;
  }
  ON_BLOCK_EXIT([&]() {
    qDebug("close %s", qUtf8Printable(filePath));
    file.close();
  });

  if (file.size() == 0) {
    // MO stores at least a header in the file. if it's completely empty the
    // file is broken
    pluginsTxtExists = false;
  }

  if (pluginsTxtExists) {
    while (!file.atEnd()) {
      QByteArray line = file.readLine();
      QString pluginName;
      if ((line.size() > 0) && (line.at(0) != '#')) {
        pluginName = QStringEncoder(QStringConverter::Encoding::System)
                         .encode(line.trimmed().constData());
      }
      if (pluginName.size() > 0) {
        pluginList->setState(pluginName, IPluginList::STATE_ACTIVE);
        plugins.removeAll(pluginName);
        // we already have the old loadorder and we ignore the positions in plugins.txt
        // (needs fix) loadOrder.append(pluginName);
      }
    }

    file.close();

    // we removed each plugin found in the file, so what's left are inactive mods
    for (const QString& pluginName : plugins) {
      pluginList->setState(pluginName, IPluginList::STATE_INACTIVE);
    }
  } else {
    for (const QString& pluginName : plugins) {
      pluginList->setState(pluginName, IPluginList::STATE_INACTIVE);
    }
  }

  return loadOrder;
}

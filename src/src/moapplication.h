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

#ifndef MOAPPLICATION_H
#define MOAPPLICATION_H

#include "applicationappearance.h"
#include "env.h"
#include "externalmessagequeue.h"
#include <QApplication>
#include <QFileSystemWatcher>
#include <QStringList>

#include <memory>
#include <optional>

class Settings;
class MOMultiProcess;
class Instance;
class PluginContainer;
class OrganizerCore;
class NexusInterface;

namespace MOBase
{
class IPluginGame;
}

class MOApplication : public QApplication
{
  Q_OBJECT

public:
  MOApplication(int& argc, char** argv);
  ~MOApplication() override;

  // called from main() only once for stuff that persists across "restarts"
  //
  void firstTimeSetup(MOMultiProcess& multiProcess);

  // called from main() each time MO "restarts", loads settings, plugins,
  // OrganizerCore and the current instance
  //
  int setup(MOMultiProcess& multiProcess, bool forceSelect);

  // shows splash, starts an api check, shows the main window and blocks until
  // MO exits
  //
  int run(MOMultiProcess& multiProcess);

  // Irreversibly close command and process-launch admission after an external
  // termination request. Existing launches retain their normal lifetime and
  // mandatory cleanup ownership.
  void beginExternalShutdown();
  bool mainEventLoopActive() const noexcept { return m_mainEventLoopActive; }

  // called from main() when MO "restarts", must clean up everything so setup()
  // starts fresh
  //
  void resetForRestart();

  // undefined if setup() wasn't called
  //
  OrganizerCore& core();

  // Completes setup-only settings writes for a terminal command-line run
  // without recording migration completion markers.
  bool finishCommandLineSetup();

  // wraps QApplication::notify() in a catch, reports errors and ignores them
  //
  bool notify(QObject* receiver, QEvent* event) override;

public slots:
  bool setStyleFile(const QString& style);

private:
  QFileSystemWatcher m_styleWatcher;
  QString m_defaultStyle;
  std::unique_ptr<ApplicationAppearance::Controller> m_appearance;
  std::optional<ApplicationAppearance::Spec> m_requestedAppearance;
  std::unique_ptr<env::ModuleNotification> m_modules;

  std::unique_ptr<Instance> m_instance;
  std::unique_ptr<Settings> m_settings;
  std::unique_ptr<NexusInterface> m_nexus;
  std::unique_ptr<PluginContainer> m_plugins;
  std::unique_ptr<OrganizerCore> m_core;
  ExternalMessageQueue m_externalMessages;
  bool m_externalDrainScheduled = false;
  bool m_externalDispatching = false;
  bool m_externalShutdownStarted = false;
  bool m_mainEventLoopActive = false;

  bool enqueueExternalMessage(const QString& message);
  void scheduleExternalMessageDrain();
  void dispatchNextExternalMessage();
  void dispatchExternalMessage(const QString& message);
  void pauseExternalMessages();
  std::unique_ptr<Instance> getCurrentInstance(bool forceSelect);
  bool applyAppearance(const ApplicationAppearance::Spec& appearance,
                       bool watchFile);
  void resetAppearance();
  void updateAppearanceWatcher(bool watchFile);
  static std::optional<int> setupInstanceLoop(Instance& currentInstance,
                                              PluginContainer& pc,
                                              Settings& settings);
  static void purgeOldFiles();
};

class MOSplash
{
public:
  MOSplash(const Settings& settings, const QString& dataPath,
           const MOBase::IPluginGame* game);

  void close();

private:
  std::unique_ptr<QSplashScreen> ss_;

  static QString getSplashPath(const Settings& settings, const QString& dataPath,
                        const MOBase::IPluginGame* game) ;
};

#endif  // MOAPPLICATION_H

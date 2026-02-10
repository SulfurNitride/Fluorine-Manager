/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <uibase/tutorialmanager.h>
#include <uibase/log.h>
#include <uibase/tutorialcontrol.h>
#include <uibase/utility.h>
#include <QApplication>
#include <QDir>
#include <QList>
#include <QString>
#include <QTimer>

namespace MOBase
{

TutorialManager* TutorialManager::s_Instance = nullptr;

TutorialManager::TutorialManager(const QString& tutorialPath, QObject* organizerCore)
    : m_TutorialPath(tutorialPath), m_OrganizerCore(organizerCore)
{}

void TutorialManager::init(const QString& tutorialPath, QObject* organizerCore)
{
  if (s_Instance != nullptr) {
    delete s_Instance;
  }
  s_Instance = new TutorialManager(tutorialPath, organizerCore);
}

TutorialManager& TutorialManager::instance()
{
  if (s_Instance == nullptr) {
    throw Exception(tr("tutorial manager not set up yet"));
  }
  return *s_Instance;
}

void TutorialManager::activateTutorial(const QString& windowName,
                                       const QString& tutorialName)
{
  std::map<QString, TutorialControl*>::iterator iter = m_Controls.find(windowName);
  if (iter != m_Controls.end()) {
    // control already visible, start tutorial right away
    iter->second->startTutorial(m_TutorialPath + tutorialName);
  } else {
    m_PendingTutorials[windowName] = tutorialName;
  }
}

void TutorialManager::finishWindowTutorial(const QString& windowName)
{
  emit windowTutorialFinished(windowName);
  //  QSettings &settings = Settings::instance().directInterface();
  //  settings.setValue(QString("CompletedWindowTutorials/") + windowName, true);
}

bool TutorialManager::hasTutorial(const QString& tutorialName)
{
  return QFile::exists(m_TutorialPath + tutorialName);
}

QWidget* TutorialManager::findControl(const QString& controlName)
{
  QWidget* mainWindow = qApp->activeWindow();
  if (mainWindow != nullptr) {
    return mainWindow->findChild<QWidget*>(controlName);
  } else {
    return nullptr;
  }
}

void TutorialManager::registerControl(const QString& windowName,
                                      TutorialControl* control)
{
  m_Controls[windowName]                    = control;
  std::map<QString, QString>::iterator iter = m_PendingTutorials.find(windowName);
  if (iter != m_PendingTutorials.end()) {
    // there is a pending tutorial for this window, display it
    QTimer::singleShot(0, [control, tutorial = m_TutorialPath + iter->second] {
      control->startTutorial(tutorial);
    });
    m_PendingTutorials.erase(iter);
  }
}

void TutorialManager::unregisterControl(const QString& windowName)
{
  std::map<QString, TutorialControl*>::iterator iter = m_Controls.find(windowName);
  if (iter != m_Controls.end()) {
    m_Controls.erase(iter);
  } else {
    log::warn("failed to remove tutorial control {}", windowName);
  }
}
}  // namespace MOBase

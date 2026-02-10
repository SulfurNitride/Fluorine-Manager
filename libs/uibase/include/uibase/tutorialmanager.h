/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef TUTORIALMANAGER_H
#define TUTORIALMANAGER_H

#include "dllimport.h"
#include <QObject>
#include <map>

namespace MOBase
{

class TutorialControl;

class QDLLEXPORT TutorialManager : public QObject
{

  Q_OBJECT

public:
  /**
   * @brief set up the tutorial manager
   * @param path to where the tutorials are stored
   */
  static void init(const QString& tutorialPath, QObject* organizerCore);

  static TutorialManager& instance();

  QObject* organizerCore() { return m_OrganizerCore; }

  /**
   * @brief registers a control that can be used to display tutorial messages in one
   * window. This this called when the window becomes visible
   * @param windowName name of the window. This is used to reference the window from
   * qml/js files, as well as for the unregister-call
   * @param control the control to register
   */
  void registerControl(const QString& windowName, TutorialControl* control);

  /**
   * @brief unregister a tutorial control. This is called when the window to which the
   * control belongs gets closed
   * @param windowName name of the control to hide
   */
  void unregisterControl(const QString& windowName);

  /**
   * @brief tests if the specified tutorial exists
   * @param tutorialName name of the tutorial to test
   * @return true if there is a tutorial with the specified name
   */
  bool hasTutorial(const QString& tutorialName);

  /**
   * @brief start a tutorial for the specified window
   * @param windowName window for which to start the tutorial
   * @param tutorialName name of the tutorial script to run
   */
  Q_INVOKABLE void activateTutorial(const QString& windowName,
                                    const QString& tutorialName);

  /**
   * @brief mark a window tutorial as completed. It will not show up again
   * @param windowName name of the window for which the tutorial completed
   */
  Q_INVOKABLE void finishWindowTutorial(const QString& windowName);

  Q_INVOKABLE QWidget* findControl(const QString& controlName);
signals:

  void windowTutorialFinished(const QString& windowName);

  void tabChanged(int index);

private:
  TutorialManager(const QString& tutorialPath, QObject* organizerCore);

private:
  static TutorialManager* s_Instance;

  //  QScriptEngine m_ScriptEngine;
  QString m_TutorialPath;
  QObject* m_OrganizerCore;

  std::map<QString, TutorialControl*> m_Controls;
  std::map<QString, QString> m_PendingTutorials;
};

}  // namespace MOBase

#endif  // TUTORIALMANAGER_H

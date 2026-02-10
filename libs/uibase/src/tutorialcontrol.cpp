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

#include <uibase/tutorialcontrol.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/tutorialmanager.h>
#include <uibase/utility.h>

#include <QAbstractButton>
#include <QAction>
#include <QBitmap>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsObject>
#include <QImage>
#include <QMenuBar>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>

#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaObject>
#include <qmetaobject.h>

namespace MOBase
{

TutorialControl::TutorialControl(const TutorialControl& reference)
    : QObject(reference.parent()), m_TargetControl(reference.m_TargetControl),
      m_Name(reference.m_Name), m_TutorialView(nullptr),
      m_Manager(TutorialManager::instance()), m_ExpectedTab(0),
      m_CurrentClickControl(nullptr)
{}

TutorialControl::TutorialControl(QWidget* targetControl, const QString& name)
    : QObject(nullptr), m_TargetControl(targetControl), m_Name(name),
      m_TutorialView(nullptr), m_Manager(TutorialManager::instance()), m_ExpectedTab(0),
      m_CurrentClickControl(nullptr)
{}

TutorialControl::~TutorialControl()
{
  m_Manager.unregisterControl(m_Name);
  finish();
}

void TutorialControl::registerControl()
{
  m_Manager.registerControl(m_Name, this);
}

void TutorialControl::resize(const QSize& size)
{
  if (m_TutorialView != nullptr) {
    m_TutorialView->resize(size.width(), size.height());
  }
}

void TutorialControl::expose(const QString& widgetName, QObject* widget)
{
  m_ExposedObjects.push_back(std::make_pair(widgetName, widget));
}

static QString canonicalPath(const QString& path)
{
#ifdef _WIN32
  std::unique_ptr<wchar_t[]> buffer(new wchar_t[32768]);
  DWORD res = ::GetShortPathNameW((wchar_t*)path.utf16(), buffer.get(), 32768);
  if (res == 0) {
    return path;
  }
  res = ::GetLongPathNameW(buffer.get(), buffer.get(), 32768);
  if (res == 0) {
    return path;
  }
  return QString::fromWCharArray(buffer.get());
#else
  QFileInfo fi(path);
  QString canonical = fi.canonicalFilePath();
  return canonical.isEmpty() ? path : canonical;
#endif
}

void TutorialControl::startTutorial(const QString& tutorial)
{
  if (m_TutorialView == nullptr) {
    m_TutorialView = new QQuickWidget(m_TargetControl);
    m_TutorialView->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_TutorialView->setAttribute(Qt::WA_TranslucentBackground);
    m_TutorialView->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_TutorialView->setClearColor(Qt::transparent);
    m_TutorialView->setStyleSheet("background: transparent");
    m_TutorialView->setObjectName("tutorialView");
    m_TutorialView->rootContext()->setContextProperty("manager", &m_Manager);

    QString qmlName =
        canonicalPath(QCoreApplication::applicationDirPath() + "/tutorials") +
        "/tutorials_" + m_Name.toLower() + ".qml";
    QUrl qmlSource = QUrl::fromLocalFile(qmlName);

    m_TutorialView->setSource(qmlSource);
    m_TutorialView->resize(m_TargetControl->width(), m_TargetControl->height());
    m_TutorialView->rootContext()->setContextProperty("scriptName", tutorial);
    m_TutorialView->rootContext()->setContextProperty("tutorialControl", this);
    m_TutorialView->rootContext()->setContextProperty("applicationWindow",
                                                      m_TargetControl);
    m_TutorialView->rootContext()->setContextProperty("organizer",
                                                      m_Manager.organizerCore());

    for (std::vector<std::pair<QString, QObject*>>::const_iterator iter =
             m_ExposedObjects.begin();
         iter != m_ExposedObjects.end(); ++iter) {
      m_TutorialView->rootContext()->setContextProperty(iter->first, iter->second);
    }
    m_TutorialView->show();
    m_TutorialView->raise();
    if (!QMetaObject::invokeMethod(m_TutorialView->rootObject(), "init")) {
      reportError(tr(
          "Tutorial failed to start, please check \"mo_interface.log\" for details."));
      m_TutorialView->close();
    }
  }
}

void TutorialControl::lockUI(bool locked)
{
  m_TutorialView->setAttribute(Qt::WA_TransparentForMouseEvents, !locked);

  QMetaObject::invokeMethod(m_TutorialView->rootObject(), "enableBackground",
                            Q_ARG(QVariant, QVariant(locked)));
}

void TutorialControl::simulateClick(int x, int y)
{
  bool wasTransparent = m_TutorialView->testAttribute(Qt::WA_TransparentForMouseEvents);
  if (!wasTransparent) {
    m_TutorialView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }
  QWidget* hitControl    = m_TargetControl->childAt(x, y);
  QPoint globalPos       = m_TargetControl->mapToGlobal(QPoint(x, y));
  QPoint hitPos          = hitControl->mapFromGlobal(globalPos);
  QMouseEvent* downEvent = new QMouseEvent(
      QEvent::MouseButtonPress, QPointF(hitPos), QPointF(globalPos),
      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent* upEvent =
      new QMouseEvent(QEvent::MouseButtonRelease, QPointF(hitPos), QPointF(globalPos),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);

  qApp->postEvent(hitControl, (QEvent*)downEvent);
  qApp->postEvent(hitControl, (QEvent*)upEvent);

  if (!wasTransparent) {
    m_TutorialView->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  }
}

QWidget* TutorialControl::getChild(const QString& name)
{
  if (m_TargetControl != nullptr) {
    return m_TargetControl->findChild<QWidget*>(name);
  } else {
    return nullptr;
  }
}

void TutorialControl::finish()
{
  if (m_TutorialView != nullptr) {
    m_TutorialView->deleteLater();
  }
  m_TutorialView = nullptr;
  m_TutorialView = nullptr;
}

QRect TutorialControl::getRect(const QString& widgetName)
{
  if (m_TargetControl != nullptr) {
    QWidget* widget = m_TargetControl->findChild<QWidget*>(widgetName);
    if (widget != nullptr) {
      QRect res  = widget->rect();
      QPoint pos = widget->mapTo(m_TargetControl, res.topLeft());
      res.moveTopLeft(pos);
      return res;
    } else {
      log::error("{} not found", widgetName);
      return QRect();
    }
  } else {
    return QRect();
  }
}

QRect TutorialControl::getActionRect(const QString& widgetName)
{
  if (m_TargetControl != nullptr) {
    QToolBar* toolBar = m_TargetControl->findChild<QToolBar*>("toolBar");
    foreach (QAction* action, toolBar->actions()) {
      if (action->objectName() == widgetName) {
        return toolBar->actionGeometry(action);
      }
    }
  }
  return QRect();
}

QRect TutorialControl::getMenuRect(const QString&)
{
  if (m_TargetControl != nullptr) {
    QMenuBar* menuBar = m_TargetControl->findChild<QMenuBar*>("menuBar");
    return menuBar->geometry();
  }
  return QRect();
}

void TutorialControl::nextTutorialStepProxy()
{

  if (m_TutorialView != nullptr) {
    QObject* background = m_TutorialView->rootObject();

    QTimer::singleShot(1, background, SLOT(nextStep()));
    lockUI(true);

    bool success = true;
    for (QMetaObject::Connection connection : m_Connections) {
      if (!disconnect(connection)) {
        success = false;
      }
    }
    m_Connections.clear();
    if (!success) {
      log::error("failed to disconnect tutorial proxy");
    }
  } else {
    log::error("failed to proceed to next tutorial step");
    finish();
  }
}

void TutorialControl::tabChangedProxy(int selected)
{
  if ((m_TutorialView != nullptr) && (selected == m_ExpectedTab)) {
    QObject* background = m_TutorialView->rootObject();
    QTimer::singleShot(1, background, SLOT(nextStep()));
    lockUI(true);
    bool success = true;
    for (QMetaObject::Connection connection : m_Connections) {
      if (!disconnect(connection)) {
        success = false;
      }
    }
    m_Connections.clear();
    if (!success) {
      log::error("failed to disconnect tab-changed proxy");
    }
  }
}

bool TutorialControl::waitForAction(const QString& actionName)
{
  if (m_TargetControl != nullptr) {
    QAction* action = m_TargetControl->findChild<QAction*>(actionName);
    if (action == nullptr) {
      log::error("no action \"{}\" in control \"{}\"", actionName, m_Name);
      return false;
    }
    if (action->isEnabled()) {
      if (action->menu() != nullptr) {
        m_Connections.append(connect(action->menu(), SIGNAL(aboutToShow()), this,
                                     SLOT(nextTutorialStepProxy())));
      } else {
        m_Connections.append(
            connect(action, SIGNAL(triggered()), this, SLOT(nextTutorialStepProxy())));
      }
      lockUI(false);
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool TutorialControl::waitForButton(const QString& buttonName)
{
  if (m_TargetControl != nullptr) {
    QAbstractButton* button = m_TargetControl->findChild<QAbstractButton*>(buttonName);
    if (button == nullptr) {
      log::error("no button \"{}\" in control \"{}\"", buttonName, m_Name);
      return false;
    }
    if (button->isEnabled()) {
      m_Connections.append(
          connect(button, SIGNAL(pressed()), this, SLOT(nextTutorialStepProxy())));
      lockUI(false);
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool TutorialControl::waitForTabOpen(const QString& tabControlName, const QString& tab)
{
  if (m_TargetControl != nullptr) {
    QTabWidget* tabWidget = m_TargetControl->findChild<QTabWidget*>(tabControlName);
    if (tabWidget == nullptr) {
      log::error("no tab widget \"{}\" in control \"{}\"", tabControlName, m_Name);
      return false;
    }
    if (tabWidget->findChild<QWidget*>(tab) == nullptr) {
      log::error("no widget \"{}\" found in tab widget \"{}\"", tab, tabControlName);
      return false;
    }
    int tabIndex = tabWidget->indexOf(tabWidget->findChild<QWidget*>(tab));
    if (tabIndex == -1) {
      log::error("widget \"{}\" does not appear to be a tab in tab widget \"{}\"", tab,
                 tabControlName);
      return false;
    }
    if (tabWidget->isEnabled()) {
      if (tabWidget->currentIndex() != tabIndex) {
        m_ExpectedTab = tabIndex;
        m_Connections.append(connect(tabWidget, SIGNAL(currentChanged(int)), this,
                                     SLOT(tabChangedProxy(int))));
        lockUI(false);
      } else {
        QObject* background = m_TutorialView->rootObject();
        QTimer::singleShot(1, background, SLOT(nextStep()));
        lockUI(true);
      }
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

const QString TutorialControl::getTabName(const QString& tabControlName)
{
  if (m_TargetControl != nullptr) {
    QTabWidget* tabWidget = m_TargetControl->findChild<QTabWidget*>(tabControlName);
    if (tabWidget == nullptr) {
      log::error("no tab widget \"{}\" in control \"{}\"", tabControlName, m_Name);
      return QString();
    }
    if (tabWidget->currentIndex() == -1) {
      return QString();
    } else {
      return tabWidget->currentWidget()->objectName();
    }
  } else {
    return QString();
  }
}
}  // namespace MOBase

#pragma once

#include <QCoreApplication>
#include <QProgressDialog>
#include <QStack>

#include <uibase/iplugininstaller.h>

#include "implementations/CodeProgress.h"
#include "QObject_unique_ptr.h"

// define this here as it's going to be used a lot by things using this class' message box wrappers.
template<class T>
T& unused_out(T&& t) { return t; }

class OMODFrameworkWrapper : public QObject
{
  Q_OBJECT

public:
  using EInstallResult = MOBase::IPluginInstaller::EInstallResult;

  OMODFrameworkWrapper(MOBase::IOrganizer* organizer, QWidget* parentWidget = nullptr);

  EInstallResult installInAnotherThread(MOBase::GuessedValue<QString>& modName, QString gameName, const QString& archiveName, const QString& version, int nexusID);

  EInstallResult install(MOBase::GuessedValue<QString>& modName, QString gameName, const QString& archiveName, const QString& version, int nexusID);

  void setParentWidget(QWidget* parentWidget);

  void onInstallationEnd(EInstallResult status, MOBase::IModInterface* mod);

protected:
  void initFrameworkSettings();

  void refreshFrameworkSettings();

  void pushTempPath(const QString& tempPath);

  void popTempPath();

signals:
  void pickModName(bool& successOut, MOBase::GuessedValue<QString>& modName);

  void createMod(MOBase::IModInterface*& modInterfaceOut, MOBase::GuessedValue<QString>& modName);

  void displayReadme(const QString& modName, const QString& readme);

  void showWaitDialog(QString message);
  void hideWaitDialog();

protected slots:
  void pickModNameSlot(bool& successOut, MOBase::GuessedValue<QString>& modName);
  void createModSlot(MOBase::IModInterface*& modInterfaceOut, MOBase::GuessedValue<QString>& modName);
  void displayReadmeSlot(const QString& modName, const QString& readme);

  void showWaitDialogSlot(QString message);
  void hideWaitDialogSlot();

private:
  __declspec(noinline) void constructorHelper();

  MOBase::IOrganizer* mMoInfo;
  QWidget* mParentWidget;

  QStack<QString> mTempPathStack;

  QObject_unique_ptr<QProgressDialog> mWaitDialog;
};

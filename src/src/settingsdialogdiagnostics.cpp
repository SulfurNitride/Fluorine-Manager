#include "settingsdialogdiagnostics.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <log.h>

using namespace MOBase;

DiagnosticsSettingsTab::DiagnosticsSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  setLogLevel();

  QString logsPath = QUrl::fromLocalFile(qApp->property("dataPath").toString() + "/" +
                                         QString::fromStdWString(AppConfig::logPath()))
                         .toString();

  ui->diagnosticsExplainedLabel->setText(
      ui->diagnosticsExplainedLabel->text()
          .replace("LOGS_FULL_PATH", logsPath)
          .replace("LOGS_DIR", QString::fromStdWString(AppConfig::logPath())));
}

void DiagnosticsSettingsTab::setLogLevel()
{
  ui->logLevelBox->clear();

  ui->logLevelBox->addItem(QObject::tr("Debug"), log::Debug);
  ui->logLevelBox->addItem(QObject::tr("Info (recommended)"), log::Info);
  ui->logLevelBox->addItem(QObject::tr("Warning"), log::Warning);
  ui->logLevelBox->addItem(QObject::tr("Error"), log::Error);

  const auto sel = settings().diagnostics().logLevel();

  for (int i = 0; i < ui->logLevelBox->count(); ++i) {
    if (ui->logLevelBox->itemData(i) == sel) {
      ui->logLevelBox->setCurrentIndex(i);
      break;
    }
  }
}

void DiagnosticsSettingsTab::update()
{
  settings().diagnostics().setLogLevel(
      static_cast<log::Levels>(ui->logLevelBox->currentData().toInt()));
}

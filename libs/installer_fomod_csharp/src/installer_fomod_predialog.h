#ifndef INSTALLER_FOMOD_PREDIALOG_H
#define INSTALLER_FOMOD_PREDIALOG_H

#include "ui_installer_fomod_csharp_predialog.h"

#include <uibase/guessedvalue.h>

#include <QCompleter>

/**
 * @brief Dialog for the installation of a simple archive
 * a simple archive is one that doesn't require any manual changes to work correctly
 **/
class InstallerFomodPredialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief constructor
   *
   * @param preset suggested name for the mod
   * @param parent parent widget
   **/
  explicit InstallerFomodPredialog(const MOBase::GuessedValue<QString>& preset,
                                   QWidget* parent = 0)
      : QDialog(parent), ui(new Ui::FomodCSharpPredialog), m_Manual(false)
  {

    ui->setupUi(this);
    setWindowTitle(preset + " - " + windowTitle());

    for (auto iter = preset.variants().begin(); iter != preset.variants().end();
         ++iter) {
      ui->nameCombo->addItem(*iter);
    }

    ui->nameCombo->setCurrentIndex(ui->nameCombo->findText(preset));
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    ui->nameCombo->completer()->setCaseSensitivity(Qt::CaseSensitive);
  }

  ~InstallerFomodPredialog() {}

  /**
   * @return true if the user requested a manual installation.
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return the (user-modified) mod name
   **/
  QString getName() const { return ui->nameCombo->currentText(); }

private slots:

  void on_okBtn_clicked() { this->accept(); }

  void on_cancelBtn_clicked() { this->reject(); }

  void on_manualBtn_clicked()
  {
    m_Manual = true;
    this->reject();
  }

private:
  std::unique_ptr<Ui::FomodCSharpPredialog> ui;
  bool m_Manual;
};

#endif

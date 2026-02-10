#ifndef INSTALLER_FOMOD_POSTDIALOG_H
#define INSTALLER_FOMOD_POSTDIALOG_H

#include "ui_installer_fomod_csharp_postdialog.h"

#include <QTextEdit>

#include "psettings.h"

/**
 * @brief Dialog for the installation of a simple archive
 * a simple archive is one that doesn't require any manual changes to work correctly
 **/
class InstallerFomodPostDialog : public QDialog
{
  Q_OBJECT

public:
  enum class Result
  {
    APPLY,
    DISCARD,
    MOVE,
  };

  /**
   * @brief constructor
   *
   * @param preset suggested name for the mod
   * @param parent parent widget
   **/
  explicit InstallerFomodPostDialog(QWidget* parent = 0)
      : QDialog(parent), ui(new Ui::FomodCSharpPostDialog)
  {
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
  }

  ~InstallerFomodPostDialog() {}

  /**
   * @return the result of this dialog if the user did not cancel.
   */
  Result result() const { return m_Result; }

  /**
   *
   */
  void setIniSettings(std::map<QString, PSettings> const& settings)
  {
    for (auto p : settings) {
      QTextEdit* widget = new QTextEdit(this);
      widget->append(p.second.toString());
      widget->setReadOnly(true);
      ui->tabWidget->addTab(widget, p.first);
    }
  }

private slots:

  void on_discardBtn_clicked()
  {
    m_Result = Result::DISCARD;
    this->accept();
  }

  void on_applyBtn_clicked()
  {
    m_Result = Result::APPLY;
    this->accept();
  }

  void on_moveBtn_clicked()
  {
    m_Result = Result::MOVE;
    this->accept();
  }

  void on_cancelBtn_clicked() { this->reject(); }

private:
  std::unique_ptr<Ui::FomodCSharpPostDialog> ui;

  Result m_Result{Result::APPLY};
};

#endif

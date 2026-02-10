#include "namedialog.h"

#include <QCompleter>

NameDialog::NameDialog(const MOBase::GuessedValue<QString>& suggestedNames, QWidget* parent)
  : QDialog(parent), ui(), mName(suggestedNames)
{
  ui.setupUi(this);

  for (const auto& name : suggestedNames.variants())
    ui.nameCombo->addItem(name);

  ui.nameCombo->setCurrentIndex(ui.nameCombo->findText(suggestedNames));

  setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
  ui.nameCombo->completer()->setCaseSensitivity(Qt::CaseSensitive);
}

QString NameDialog::getName() const
{
  return mName;
}

void NameDialog::on_okBtn_clicked()
{
  accept();
}

void NameDialog::on_cancelBtn_clicked()
{
  reject();
}

void NameDialog::on_nameCombo_currentTextChanged(const QString& text)
{
  mName = text;
}

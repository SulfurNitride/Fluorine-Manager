#pragma once

#include <QDialog>

#include <uibase/guessedvalue.h>

#include "ui_namedialog.h"

class NameDialog : public QDialog
{
  Q_OBJECT

public:
  NameDialog(const MOBase::GuessedValue<QString>& suggestedNames, QWidget* parent = nullptr);

  QString getName() const;

private slots:
  void on_okBtn_clicked();

  void on_cancelBtn_clicked();

  void on_nameCombo_currentTextChanged(const QString& text);

private:
  Ui::NameDialog ui;
  QString mName;
};

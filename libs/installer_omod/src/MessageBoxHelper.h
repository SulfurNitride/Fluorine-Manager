#pragma once

#include <QMessageBox>

class MessageBoxHelper : public QObject
{
  Q_OBJECT

public:
  MessageBoxHelper();

  QMessageBox::StandardButton critical(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

  QMessageBox::StandardButton information(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

  QMessageBox::StandardButton question(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

  QMessageBox::StandardButton warning(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

signals:
  void criticalMessageBoxSignal(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

  void informationMessageBoxSignal(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

  void questionMessageBoxSignal(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

  void warningMessageBoxSignal(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons = QMessageBox::Ok, QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

private slots:
  void criticalMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton);

  void informationMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton);

  void questionMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton);

  void warningMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton);
};

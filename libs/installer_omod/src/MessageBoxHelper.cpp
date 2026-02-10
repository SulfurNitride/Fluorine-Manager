#include "MessageBoxHelper.h"

#include <QApplication>

MessageBoxHelper::MessageBoxHelper()
{
  moveToThread(QApplication::instance()->thread());

  connect(this, &MessageBoxHelper::criticalMessageBoxSignal, this, &MessageBoxHelper::criticalMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
  connect(this, &MessageBoxHelper::informationMessageBoxSignal, this, &MessageBoxHelper::informationMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
  connect(this, &MessageBoxHelper::questionMessageBoxSignal, this, &MessageBoxHelper::questionMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
  connect(this, &MessageBoxHelper::warningMessageBoxSignal, this, &MessageBoxHelper::warningMessageBoxSlot, Qt::ConnectionType::BlockingQueuedConnection);
}

QMessageBox::StandardButton MessageBoxHelper::critical(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit criticalMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

QMessageBox::StandardButton MessageBoxHelper::information(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit informationMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

QMessageBox::StandardButton MessageBoxHelper::question(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit questionMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

QMessageBox::StandardButton MessageBoxHelper::warning(QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  QMessageBox::StandardButton response;
  emit warningMessageBoxSignal(response, parent, title, text, buttons, defaultButton);
  return response;
}

void MessageBoxHelper::criticalMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = QMessageBox::critical(parent, title, text, buttons, defaultButton);
}

void MessageBoxHelper::informationMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = QMessageBox::information(parent, title, text, buttons, defaultButton);
}

void MessageBoxHelper::questionMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = QMessageBox::question(parent, title, text, buttons, defaultButton);
}

void MessageBoxHelper::warningMessageBoxSlot(QMessageBox::StandardButton& standardButtonOut, QWidget* parent, const QString& title, const QString& text, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
  standardButtonOut = QMessageBox::warning(parent, title, text, buttons, defaultButton);
}

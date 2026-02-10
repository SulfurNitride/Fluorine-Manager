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

#include <uibase/questionboxmemory.h>
#include <uibase/log.h>
#include "ui_questionboxmemory.h"

#include <QApplication>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QSettings>
#include <QStyle>

namespace MOBase
{

static QMutex g_mutex;
static QuestionBoxMemory::GetButton g_get;
static QuestionBoxMemory::SetWindowButton g_setWindow;
static QuestionBoxMemory::SetFileButton g_setFile;

QuestionBoxMemory::QuestionBoxMemory(QWidget* parent, const QString& title,
                                     const QString& text, QString const* filename,
                                     const QDialogButtonBox::StandardButtons buttons,
                                     QDialogButtonBox::StandardButton defaultButton)
    : QDialog(parent), ui(new Ui::QuestionBoxMemory), m_Button(QDialogButtonBox::Cancel)
{
  ui->setupUi(this);

  setWindowFlag(Qt::WindowType::WindowContextHelpButtonHint, false);
  setWindowTitle(title);

  QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion);
  ui->iconLabel->setPixmap(icon.pixmap(128));
  ui->messageLabel->setText(text);

  if (filename == nullptr) {
    // delete the 2nd check box
    QCheckBox* box = ui->rememberForCheckBox;
    box->parentWidget()->layout()->removeWidget(box);
    delete box;
  } else {
    ui->rememberForCheckBox->setText(ui->rememberForCheckBox->text().arg(*filename));
  }

  ui->buttonBox->setStandardButtons(buttons);

  if (defaultButton != QDialogButtonBox::NoButton) {
    ui->buttonBox->button(defaultButton)->setDefault(true);
  }

  connect(ui->buttonBox, SIGNAL(clicked(QAbstractButton*)), this,
          SLOT(buttonClicked(QAbstractButton*)));
}

QuestionBoxMemory::~QuestionBoxMemory() = default;

void QuestionBoxMemory::setCallbacks(GetButton get, SetWindowButton setWindow,
                                     SetFileButton setFile)
{
  QMutexLocker locker(&g_mutex);

  g_get       = get;
  g_setWindow = setWindow;
  g_setFile   = setFile;
}

void QuestionBoxMemory::buttonClicked(QAbstractButton* button)
{
  m_Button = ui->buttonBox->standardButton(button);
}

QDialogButtonBox::StandardButton
QuestionBoxMemory::query(QWidget* parent, const QString& windowName,
                         const QString& title, const QString& text,
                         QDialogButtonBox::StandardButtons buttons,
                         QDialogButtonBox::StandardButton defaultButton)
{
  return queryImpl(parent, windowName, nullptr, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
QuestionBoxMemory::query(QWidget* parent, const QString& windowName,
                         const QString& fileName, const QString& title,
                         const QString& text, QDialogButtonBox::StandardButtons buttons,
                         QDialogButtonBox::StandardButton defaultButton)
{
  return queryImpl(parent, windowName, &fileName, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
QuestionBoxMemory::queryImpl(QWidget* parent, const QString& windowName,
                             const QString* fileName, const QString& title,
                             const QString& text,
                             QDialogButtonBox::StandardButtons buttons,
                             QDialogButtonBox::StandardButton defaultButton)
{
  QMutexLocker locker(&g_mutex);

  const auto button = getMemory(windowName, (fileName ? *fileName : ""));
  if (button != NoButton) {
    log::debug("{}: not asking because user always wants response {}",
               windowName + (fileName ? QString("/") + *fileName : ""),
               buttonToString(button));

    return button;
  }

  QuestionBoxMemory dialog(parent, title, text, fileName, buttons, defaultButton);
  dialog.exec();

  if (dialog.m_Button != QDialogButtonBox::Cancel) {
    if (dialog.ui->rememberCheckBox->isChecked()) {
      setWindowMemory(windowName, dialog.m_Button);
    }

    if (fileName != nullptr && dialog.ui->rememberForCheckBox->isChecked()) {
      setFileMemory(windowName, *fileName, dialog.m_Button);
    }
  }

  return dialog.m_Button;
}

void QuestionBoxMemory::setWindowMemory(const QString& windowName, Button b)
{
  log::debug("remembering choice {} for window {}", buttonToString(b), windowName);

  g_setWindow(windowName, b);
}

void QuestionBoxMemory::setFileMemory(const QString& windowName,
                                      const QString& filename, Button b)
{
  log::debug("remembering choice {} for file {}", buttonToString(b),
             windowName + "/" + filename);

  g_setFile(windowName, filename, b);
}

QuestionBoxMemory::Button QuestionBoxMemory::getMemory(const QString& windowName,
                                                       const QString& filename)
{
  return g_get(windowName, filename);
}

QString QuestionBoxMemory::buttonToString(Button b)
{
  using BB = QDialogButtonBox;

  static const std::map<Button, QString> map = {
      {BB::NoButton, "none"},
      {BB::Ok, "ok"},
      {BB::Save, "save"},
      {BB::SaveAll, "saveall"},
      {BB::Open, "open"},
      {BB::Yes, "yes"},
      {BB::YesToAll, "yestoall"},
      {BB::No, "no"},
      {BB::NoToAll, "notoall"},
      {BB::Abort, "abort"},
      {BB::Retry, "retry"},
      {BB::Ignore, "ignore"},
      {BB::Close, "close"},
      {BB::Cancel, "cancel"},
      {BB::Discard, "discard"},
      {BB::Help, "help"},
      {BB::Apply, "apply"},
      {BB::Reset, "reset"},
      {BB::RestoreDefaults, "restoredefaults"}};

  auto itor = map.find(b);

  if (itor == map.end()) {
    return QString("0x%1").arg(static_cast<int>(b), 0, 16);
  } else {
    return QString("'%1' (0x%2)").arg(itor->second).arg(static_cast<int>(b), 0, 16);
  }
}

}  // namespace MOBase

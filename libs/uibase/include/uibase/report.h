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

#ifndef REPORT_H
#define REPORT_H

#include "dllimport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QList>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <wchar.h>

namespace Ui
{
class TaskDialog;
}

namespace MOBase
{

class ExpanderWidget;

// Convenience function displaying an error message box. This function uses
// WinAPI if no Qt Window is available yet or QMessageBox otherwise.
//
QDLLEXPORT void reportError(const QString& message);

// shows a critical message box that's raised to the top of the zorder, useful
// for messages without a main window, which sometimes makes them pop up behind
// all other windows
//
// the dialog is not topmost, it's just raised once when shown
//
QDLLEXPORT void criticalOnTop(const QString& message);

struct QDLLEXPORT TaskDialogButton
{
  QString text, description;
  QMessageBox::StandardButton button;

  TaskDialogButton(QString text, QString description,
                   QMessageBox::StandardButton button);
  TaskDialogButton(QString text, QMessageBox::StandardButton button);
};

class QDLLEXPORT TaskDialog
{
public:
  TaskDialog(QWidget* parent = nullptr, QString title = {});
  ~TaskDialog();

  TaskDialog& title(const QString& s);
  TaskDialog& main(const QString& s);
  TaskDialog& content(const QString& s);
  TaskDialog& details(const QString& s);
  TaskDialog& icon(QMessageBox::Icon i);
  TaskDialog& button(TaskDialogButton b);
  TaskDialog& remember(const QString& action, const QString& file = {});

  void addContent(QWidget* w);
  void setWidth(int w);

  QMessageBox::StandardButton exec();

private:
  std::unique_ptr<QDialog> m_dialog;
  std::unique_ptr<Ui::TaskDialog> ui;
  QString m_title, m_main, m_content, m_details;
  QMessageBox::Icon m_icon;
  std::vector<TaskDialogButton> m_buttons;
  QMessageBox::StandardButton m_result;
  std::unique_ptr<ExpanderWidget> m_expander;
  int m_width;

  QString m_rememberAction, m_rememberFile;
  QCheckBox* m_rememberCheck;
  QComboBox* m_rememberCombo;

  QMessageBox::StandardButton checkMemory() const;
  void rememberChoice();

  void setDialog();
  void setWidgets();
  void setChoices();
  void setButtons();
  void setStandardButtons();
  void setCommandButtons();
  void setDetails();

  QColor detailsColor() const;
  QPixmap standardIcon(QMessageBox::Icon icon) const;
  void setVisibleLines(QPlainTextEdit* w, int lines);
  void setFontPercent(QWidget* w, double p);
};

}  // namespace MOBase

#endif  // REPORT_H

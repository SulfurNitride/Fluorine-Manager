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

#ifndef TEXTVIEWER_H
#define TEXTVIEWER_H

#include "dllimport.h"
#include <QDialog>
#include <QTabWidget>
#include <QTextEdit>
#include <set>

namespace Ui
{
class TextViewer;
}

namespace MOBase
{

class FindDialog;

/**
 * @brief rudimentary tabbed text editor
 **/
class QDLLEXPORT TextViewer : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief constructor
   * @param parent parent widget
   **/
  explicit TextViewer(const QString& title, QWidget* parent = 0);

  ~TextViewer();

  /**
   * @brief set the description text to inform the user what he's editing
   *
   * @param description the description to set
   **/
  void setDescription(const QString& description);

  /**
   * @brief add a new tab with the specified file open
   *
   * @param fileName name of the file to open
   * @param writable if true, the file can be modified
   **/
  void addFile(const QString& fileName, bool writable);

protected:
  void closeEvent(QCloseEvent* event);
  bool eventFilter(QObject* obj, QEvent* event);

private slots:

  void saveFile();
  void modified();
  void patternChanged(QString newPattern);
  void findNext();
  void showWhitespaceChanged(int state);

private:
  void saveFile(const QTextEdit* editor);
  void find();

private:
  Ui::TextViewer* ui;
  QTabWidget* m_EditorTabs;
  std::set<QTextEdit*> m_Modified;
  FindDialog* m_FindDialog;
  QString m_FindPattern;
};

}  // namespace MOBase

#endif  // TEXTVIEWER_H

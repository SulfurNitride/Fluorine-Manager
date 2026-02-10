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

#ifndef QUESTIONBOXMEMORY_H
#define QUESTIONBOXMEMORY_H

#include "dllimport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QList>
#include <QObject>
#include <QString>

class QAbstractButton;
class QMutex;
class QSettings;
class QWidget;

namespace Ui
{
class QuestionBoxMemory;
}

namespace MOBase
{

class QDLLEXPORT QuestionBoxMemory : public QDialog
{
  Q_OBJECT

public:
  using Button               = QDialogButtonBox::StandardButton;
  static const auto NoButton = QDialogButtonBox::NoButton;

  using GetButton       = std::function<Button(const QString&, const QString&)>;
  using SetWindowButton = std::function<void(const QString&, Button)>;
  using SetFileButton   = std::function<void(const QString&, const QString&, Button)>;

  ~QuestionBoxMemory();

  // QuestionBoxMemory needs to access the settings, but they're only in
  // the modorganizer project; the only way to avoid accessing the ini file
  // directly is to use callbacks registered in Settings' constructor
  //
  static void setCallbacks(GetButton get, SetWindowButton setWindow,
                           SetFileButton setFile);

  static Button
  query(QWidget* parent, const QString& windowName, const QString& title,
        const QString& text,
        QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Yes |
                                                    QDialogButtonBox::No,
        QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);

  static Button
  query(QWidget* parent, const QString& windowName, const QString& fileName,
        const QString& title, const QString& text,
        QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Yes |
                                                    QDialogButtonBox::No,
        QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);

  static void setWindowMemory(const QString& windowName, Button b);

  static void setFileMemory(const QString& windowName, const QString& filename,
                            Button b);

  static Button getMemory(const QString& windowName, const QString& filename);

  static QString buttonToString(Button b);

private slots:
  void buttonClicked(QAbstractButton* button);

private:
  explicit QuestionBoxMemory(QWidget* parent, const QString& title, const QString& text,
                             const QString* filename,
                             const QDialogButtonBox::StandardButtons buttons,
                             QDialogButtonBox::StandardButton defaultButton);

private:
  static Button queryImpl(
      QWidget* parent, const QString& windowName, const QString* fileName,
      const QString& title, const QString& text,
      QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Yes |
                                                  QDialogButtonBox::No,
      QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::NoButton);

  std::unique_ptr<Ui::QuestionBoxMemory> ui;
  QDialogButtonBox::StandardButton m_Button;
};

}  // namespace MOBase

#endif  // QUESTIONBOXMEMORY_H

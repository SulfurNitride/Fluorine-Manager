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

#ifndef TUTORABLEDIALOG_H
#define TUTORABLEDIALOG_H

#include "dllimport.h"
#include "tutorialcontrol.h"
#include <QDialog>
#include <QResizeEvent>
#include <QShowEvent>

namespace MOBase
{

/**
 * @brief A dialog for which a tutorial can be displayed. Dialogs should derive from
 * this instead of QDialog and delegate all showEvent- and resizeEvent-calls to
 * TutorableDialog for tutorials to work
 */
class QDLLEXPORT TutorableDialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief constructor
   * @param name a unique name for this dialog type. This is used to refer to the dialog
   * from tutorials
   * @param parent the parent widget of the dialog
   */
  explicit TutorableDialog(const QString& name, QWidget* parent = 0);

signals:

public slots:

protected:
  void showEvent(QShowEvent* event);
  void resizeEvent(QResizeEvent* event);

private:
  TutorialControl m_Tutorial;
};

}  // namespace MOBase

#endif  // TUTORABLEDIALOG_H

/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <uibase/tutorabledialog.h>

namespace MOBase
{

TutorableDialog::TutorableDialog(const QString& name, QWidget* parent)
    : QDialog(parent), m_Tutorial(this, name)
{}

void TutorableDialog::showEvent(QShowEvent* event)
{
  m_Tutorial.registerControl();
  QDialog::showEvent(event);
}

void TutorableDialog::resizeEvent(QResizeEvent* event)
{
  m_Tutorial.resize(event->size());
  QDialog::resizeEvent(event);
}
}  // namespace MOBase

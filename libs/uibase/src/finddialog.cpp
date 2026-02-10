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

#include <uibase/finddialog.h>
#include "ui_finddialog.h"

namespace MOBase
{

FindDialog::FindDialog(QWidget* parent) : QDialog(parent), ui(new Ui::FindDialog)
{
  ui->setupUi(this);
}

FindDialog::~FindDialog()
{
  delete ui;
}

void FindDialog::on_nextBtn_clicked()
{
  emit findNext();
}

void FindDialog::on_patternEdit_textChanged(const QString& pattern)
{
  emit patternChanged(pattern);
}

void FindDialog::on_closeBtn_clicked()
{
  this->close();
}
}  // namespace MOBase

/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BAINCOMPLEXINSTALLERDIALOG_H
#define BAINCOMPLEXINSTALLERDIALOG_H

#include <vector>

#include <QDialog>

#include <uibase/ifiletree.h>

namespace Ui
{
class MultiArchiveDialog;
}

/**
 *
 **/
class MultiArchiveDialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @brief Constructor
   *
   * @param tree the directory tree of the archive. The caller is resonsible to verify
   *this actually qualifies as a bain installer
   * @param modName proposed name for the mod. The dialog allows the user to change this
   * @param packageTXT path to the extracted package.txt file or an empty string if
   *there is none
   * @param parent parent widget
   **/
  explicit MultiArchiveDialog(
      std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> entries,
      QWidget* parent);
  ~MultiArchiveDialog();

  /**
   * @return bool true if the user requested the manual dialog
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return QString the (user-modified) name to be used for the mod
   **/
  auto selectedEntry() const { return m_SelectedEntry; }

private slots:

  void on_manualBtn_clicked();
  void on_cancelBtn_clicked();

private:
  Ui::MultiArchiveDialog* ui;
  bool m_Manual;
  std::shared_ptr<const MOBase::FileTreeEntry> m_SelectedEntry;
};

#endif  // BAINCOMPLEXINSTALLERDIALOG_H

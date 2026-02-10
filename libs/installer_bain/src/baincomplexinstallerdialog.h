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

#include <uibase/guessedvalue.h>
#include <uibase/ifiletree.h>
#include <uibase/tutorabledialog.h>

namespace Ui
{
class BainComplexInstallerDialog;
}

/**
 * @brief Dialog to choose from options offered by a (complex) bain package
 **/
class BainComplexInstallerDialog : public MOBase::TutorableDialog
{
  Q_OBJECT

public:
  /**
   * @brief Constructor
   *
   * @param tree the directory tree of the archive. The caller is resonsible to verify
   *this actually qualifies as a bain installer
   * @param modName proposed name for the mod. The dialog allows the user to change this
   * @param defaultOptions The default options to select. Can contains options not
   *actually present.
   * @param packageTXT path to the extracted package.txt file or an empty string if
   *there is none
   * @param parent parent widget
   **/
  explicit BainComplexInstallerDialog(
      const std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>& subpackages,
      const MOBase::GuessedValue<QString>& modName, const QStringList& defaultOptions,
      const QString& packageTXT, QWidget* parent);
  ~BainComplexInstallerDialog();

  /**
   * @return bool true if the user requested the manual dialog
   **/
  bool manualRequested() const { return m_Manual; }

  /**
   * @return QString the (user-modified) name to be used for the mod
   **/
  QString getName() const;

  /**
   * @brief Remove from the given tree the option not selected by the user.
   *
   * @param tree The input tree.
   *
   * @return the list of options selected by the user.
   **/
  QStringList updateTree(std::shared_ptr<MOBase::IFileTree>& tree);

private slots:

  void on_manualBtn_clicked();

  void on_okBtn_clicked();

  void on_cancelBtn_clicked();

  void on_packageBtn_clicked();

private:
  Ui::BainComplexInstallerDialog* ui;

  bool m_Manual;
  QString m_PackageTXT;
};

#endif  // BAINCOMPLEXINSTALLERDIALOG_H

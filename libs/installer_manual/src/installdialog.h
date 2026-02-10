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

#ifndef INSTALLDIALOG_H
#define INSTALLDIALOG_H

#include <QDialog>
#include <QProgressDialog>
#include <QTreeWidgetItem>
#include <QUuid>

#include <uibase/game_features/moddatachecker.h>
#include <uibase/guessedvalue.h>
#include <uibase/ifiletree.h>
#include <uibase/iplugingame.h>
#include <uibase/tutorabledialog.h>

#include "archivetree.h"

namespace Ui
{
class InstallDialog;
}

/**
 * a dialog presented to manually define how a mod is to be installed. It provides
 * a tree view of the file contents that can modified directly
 **/
class InstallDialog : public MOBase::TutorableDialog
{
  Q_OBJECT

public:
  /**
   * @brief Create a new install dialog for the given tree. The tree
   * is "own" by the dialog, i.e., any change made by the user is immediately
   * reflected to the given tree, except for the changes to the root.
   *
   * @param tree Tree structure describing the original archive structure.
   * @param modName Name of the mod. The name can be modified through the dialog.
   * @param modDataChecker The mod data checker to use to check.
   * @param dataName The name of the data folder for the game.
   * @param parent Parent widget.
   **/
  explicit InstallDialog(std::shared_ptr<MOBase::IFileTree> tree,
                         const MOBase::GuessedValue<QString>& modName,
                         std::shared_ptr<const MOBase::ModDataChecker> modDataChecker,
                         const QString& dataName, QWidget* parent = 0);
  ~InstallDialog();

  /**
   * @brief retrieve the (modified) mod name
   *
   * @return updated mod name
   **/
  QString getModName() const;

  /**
   * @brief Retrieve the user-modified directory structure.
   *
   * @return the new tree represented by this dialog, which can be a new
   *     tree or a subtree of the original tree.
   **/
  std::shared_ptr<MOBase::IFileTree> getModifiedTree() const;

signals:

  /**
   * @brief Signal emitted when user request the file corresponding
   *     to the given entry to be opened.
   *
   * @param entry Entry corresponding to the file to open.
   */
  void openFile(const MOBase::FileTreeEntry* entry);

private:
  bool testForProblem();
  void updateProblems();
  void createDirectoryUnder(ArchiveTreeWidgetItem* treeItem);

private slots:

  // Automatic slots that are directly bound to the UI:
  void on_treeContent_customContextMenuRequested(QPoint pos);
  void on_cancelButton_clicked();
  void on_okButton_clicked();

private:
  Ui::InstallDialog* ui;

  std::shared_ptr<const MOBase::ModDataChecker> m_Checker;

  // Name of the "data" directory:
  QString m_DataFolderName;

  // the tree root is the initial root that will never change (should be const
  // but cannot be since the parent tree cannot be constructed in the member
  // initializer list)
  //
  // the tree root is not actually added to the tree, but is used to maintain
  // the state of the tree and not lose entries when unsetting data root
  //
  ArchiveTreeWidget* m_Tree;
  ArchiveTreeWidgetItem* m_TreeRoot;
  QLabel* m_ProblemLabel;
};

#endif  // INSTALLDIALOG_H

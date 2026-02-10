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

#include "installdialog.h"
#include "ui_installdialog.h"

#include <QCompleter>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>

#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/utility.h>

using namespace MOBase;

InstallDialog::InstallDialog(
    std::shared_ptr<IFileTree> tree, const GuessedValue<QString>& modName,
    std::shared_ptr<const MOBase::ModDataChecker> modDataChecker,
    const QString& dataName, QWidget* parent)
    : TutorableDialog("InstallDialog", parent), ui(new Ui::InstallDialog),
      m_Checker(modDataChecker), m_DataFolderName(dataName)
{

  ui->setupUi(this);

  for (auto iter = modName.variants().begin(); iter != modName.variants().end();
       ++iter) {
    ui->nameCombo->addItem(*iter);
  }

  ui->nameCombo->setCurrentIndex(ui->nameCombo->findText(modName));
  ui->nameCombo->completer()->setCaseSensitivity(Qt::CaseSensitive);

  m_ProblemLabel = ui->problemLabel;

  m_Tree     = ui->treeContent;
  m_TreeRoot = new ArchiveTreeWidgetItem(tree);
  m_Tree->setup(m_DataFolderName);
  connect(m_Tree, &ArchiveTreeWidget::treeChanged, [this] {
    updateProblems();
  });

  m_Tree->setDataRoot(m_TreeRoot);
}

InstallDialog::~InstallDialog()
{
  delete ui;
}

QString InstallDialog::getModName() const
{
  return ui->nameCombo->currentText();
}

/**
 * @brief Retrieve the user-modified directory structure.
 *
 * @return the new tree represented by this dialog, which can be a new
 *     tree or a subtree of the original tree.
 **/
std::shared_ptr<MOBase::IFileTree> InstallDialog::getModifiedTree() const
{
  return m_Tree->root()->entry()->astree();
}

bool InstallDialog::testForProblem()
{
  if (!m_Checker) {
    return true;
  }
  return m_Checker->dataLooksValid(m_Tree->root()->entry()->astree()) ==
         ModDataChecker::CheckReturn::VALID;
}

void InstallDialog::updateProblems()
{
  if (!m_Checker) {
    m_Tree->setStyleSheet("QTreeWidget { border: none; }");
    m_ProblemLabel->setText(
        tr("Cannot check the content of <%1>.").arg(m_DataFolderName));
    m_ProblemLabel->setToolTip(tr("The plugin for the current game does not provide a "
                                  "way to check the content of <%1>.")
                                   .arg(m_DataFolderName));
    m_ProblemLabel->setStyleSheet("color: darkYellow;");
  } else if (testForProblem()) {
    m_Tree->setStyleSheet(
        "QTreeWidget { border: 1px solid darkGreen; border-radius: 2px; }");
    m_ProblemLabel->setText(
        tr("The content of <%1> looks valid.").arg(m_DataFolderName));
    m_ProblemLabel->setToolTip(
        tr("The content of <%1> seems valid for the current game.")
            .arg(m_DataFolderName));
    m_ProblemLabel->setStyleSheet("color: darkGreen;");
  } else {
    m_Tree->setStyleSheet("QTreeWidget { border: 1px solid red; border-radius: 2px; }");
    m_ProblemLabel->setText(
        tr("The content of <%1> does not look valid.").arg(m_DataFolderName));
    m_ProblemLabel->setToolTip(
        tr("The content of <%1> is probably not valid for the current game.")
            .arg(m_DataFolderName));
    m_ProblemLabel->setStyleSheet("color: red;");
  }
}

void InstallDialog::createDirectoryUnder(ArchiveTreeWidgetItem* item)
{
  // Should never happen if we customize the context menu depending
  // on the item:
  if (!item->entry()->isDir()) {
    reportError(tr("Cannot create directory under a file."));
    return;
  }

  // Retrieve the directory:
  auto fileTree = item->entry()->astree();

  bool ok        = false;
  QString result = QInputDialog::getText(this, tr("Enter a directory name"), tr("Name"),
                                         QLineEdit::Normal, QString(), &ok);
  result         = result.trimmed();

  if (ok && !result.isEmpty()) {

    // If a file with this name already exists:
    if (fileTree->exists(result)) {
      reportError(tr("A directory or file with that name already exists."));
      return;
    }

    item->setExpanded(true);
    auto* newItem = m_Tree->addDirectory(item, result);
    m_Tree->scrollToItem(newItem);
  }
}

void InstallDialog::on_treeContent_customContextMenuRequested(QPoint pos)
{
  ArchiveTreeWidgetItem* selectedItem =
      static_cast<ArchiveTreeWidgetItem*>(m_Tree->itemAt(pos));
  if (selectedItem == nullptr) {
    return;
  }

  QMenu menu;

  if (selectedItem != m_Tree->root() && selectedItem->entry()->isDir()) {
    menu.addAction(tr("Set as <%1> directory").arg(m_DataFolderName),
                   [this, selectedItem]() {
                     m_Tree->setDataRoot(selectedItem);
                   });
  }

  if (m_Tree->root()->entry() != m_TreeRoot->entry()) {
    menu.addAction(tr("Unset <%1> directory").arg(m_DataFolderName), [this]() {
      m_Tree->setDataRoot(m_TreeRoot);
    });
  }

  // Add a separator if not empty:
  if (!menu.isEmpty()) {
    menu.addSeparator();
  }

  if (selectedItem->entry()->isDir()) {
    menu.addAction(tr("Create directory..."), [this, selectedItem]() {
      createDirectoryUnder(selectedItem);
    });
  } else {
    menu.addAction(tr("&Open"), [this, selectedItem]() {
      emit openFile(selectedItem->entry().get());
    });
  }
  menu.exec(m_Tree->mapToGlobal(pos));
}

void InstallDialog::on_okButton_clicked()
{
  if (!testForProblem()) {
    if (QMessageBox::question(
            this, tr("Continue?"),
            tr("This mod was probably NOT set up correctly, most likely it will NOT "
               "work. "
               "You should first correct the directory layout using the content-tree."),
            QMessageBox::Ignore | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Cancel) {
      return;
    }
  }
  this->accept();
}

void InstallDialog::on_cancelButton_clicked()
{
  this->reject();
}

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
#include "ui_baincomplexinstallerdialog.h"

#include <QCompleter>

#include <uibase/textviewer.h>

#include "baincomplexinstallerdialog.h"

using namespace MOBase;

BainComplexInstallerDialog::BainComplexInstallerDialog(
    const std::vector<std::shared_ptr<const MOBase::FileTreeEntry>>& subpackages,
    const GuessedValue<QString>& modName, const QStringList& defaultOptions,
    const QString& packageTXT, QWidget* parent)
    : TutorableDialog("BainInstaller", parent), ui(new Ui::BainComplexInstallerDialog),
      m_Manual(false), m_PackageTXT(packageTXT)
{
  ui->setupUi(this);

  for (auto iter = modName.variants().begin(); iter != modName.variants().end();
       ++iter) {
    ui->nameCombo->addItem(*iter);
  }

  ui->nameCombo->setCurrentIndex(ui->nameCombo->findText(modName));

  for (const auto& subpackage : subpackages) {

    auto name = subpackage->name();

    QListWidgetItem* item = new QListWidgetItem(name, ui->optionsList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

    if (name.mid(0, 2) == "00" || defaultOptions.contains(name, Qt::CaseInsensitive)) {
      item->setCheckState(Qt::Checked);
    } else {
      item->setCheckState(Qt::Unchecked);
    }

    ui->optionsList->addItem(item);
  }

  ui->packageBtn->setEnabled(!packageTXT.isEmpty());
  ui->nameCombo->completer()->setCaseSensitivity(Qt::CaseSensitive);
}

BainComplexInstallerDialog::~BainComplexInstallerDialog()
{
  delete ui;
}

QString BainComplexInstallerDialog::getName() const
{
  return ui->nameCombo->currentText();
}

QStringList BainComplexInstallerDialog::updateTree(std::shared_ptr<IFileTree>& tree)
{
  // Retrieve the list of selected names:
  std::set<QString, FileNameComparator> selectedNames;
  for (int i = 0; i < ui->optionsList->count(); ++i) {
    QListWidgetItem* item = ui->optionsList->item(i);
    if (item->checkState() == Qt::Checked) {
      selectedNames.insert(item->text());
    }
  }

  // Create a new empty tree and merge all the selected folder in it:
  auto newTree = tree->createOrphanTree();
  for (auto& entry : *tree) {
    if (entry->isDir() && selectedNames.count(entry->name()) > 0) {
      newTree->merge(entry->astree());
    }
  }

  tree = newTree;

  return QStringList(selectedNames.begin(), selectedNames.end());
}

void BainComplexInstallerDialog::on_okBtn_clicked()
{
  this->accept();
}

void BainComplexInstallerDialog::on_cancelBtn_clicked()
{
  this->reject();
}

void BainComplexInstallerDialog::on_manualBtn_clicked()
{
  m_Manual = true;
  this->reject();
}

void BainComplexInstallerDialog::on_packageBtn_clicked()
{
  TextViewer viewer(m_PackageTXT, this);
  viewer.setDescription("");
  viewer.addFile(m_PackageTXT, false);
  viewer.exec();
}

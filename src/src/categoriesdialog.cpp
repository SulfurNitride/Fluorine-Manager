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

#include "categoriesdialog.h"
#include "categories.h"
#include "categoryassignmentpolicy.h"
#include "categoryimportdialog.h"
#include "messagedialog.h"
#include "nexusinterface.h"
#include "settings.h"
#include "ui_categoriesdialog.h"
#include "utility.h"
#include <QItemDelegate>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpressionValidator>
#include <memory>

class NewIDValidator : public QIntValidator
{
public:
  NewIDValidator(const std::set<int>& ids, const int& durableHighWater)
      : m_UsedIDs(ids), m_DurableHighWater(durableHighWater)
  {}
  State validate(QString& input, int& pos) const override
  {
    State const intRes = QIntValidator::validate(input, pos);
    if (intRes == Acceptable) {
      bool ok = false;
      int const id  = input.toInt(&ok);
      if (m_UsedIDs.contains(id) ||
          !CategoryAssignmentPolicy::mayAssignCategoryId(
              m_DurableHighWater, id, std::nullopt)) {
        return QValidator::Intermediate;
      }
    }
    return intRes;
  }

private:
  const std::set<int>& m_UsedIDs;
  const int& m_DurableHighWater;
};

class ExistingIDValidator : public QIntValidator
{
public:
  ExistingIDValidator(const std::set<int>& ids) : m_UsedIDs(ids) {}
  State validate(QString& input, int& pos) const override
  {
    State const intRes = QIntValidator::validate(input, pos);
    if (intRes == Acceptable) {
      bool ok = false;
      int const id  = input.toInt(&ok);
      if ((id == 0) || (m_UsedIDs.contains(id))) {
        return QValidator::Acceptable;
      } else {
        return QValidator::Intermediate;
      }
    } else {
      return intRes;
    }
  }

private:
  const std::set<int>& m_UsedIDs;
};

class ValidatingDelegate : public QItemDelegate
{

public:
  ValidatingDelegate(QObject* parent, QValidator* validator)
      : QItemDelegate(parent), m_Validator(validator)
  {}

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                        const QModelIndex&) const override
  {
    QLineEdit* edit = new QLineEdit(parent);
    edit->setValidator(m_Validator);
    return edit;
  }
  void setModelData(QWidget* editor, QAbstractItemModel* model,
                            const QModelIndex& index) const override
  {
    QLineEdit* edit  = qobject_cast<QLineEdit*>(editor);
    int pos          = 0;
    QString editText = edit->text();
    if (m_Validator->validate(editText, pos) == QValidator::Acceptable) {
      // Set ID and Parent ID as ints so sorting works as expected
      if (index.column() == 0 || index.column() == 2) {
        model->setData(index, editText.toInt());
        return;
      }
      QItemDelegate::setModelData(editor, model, index);
    }
  }

private:
  QValidator* m_Validator;
};

CategoriesDialog::CategoriesDialog(QWidget* parent)
    : TutorableDialog("Categories", parent), ui(new Ui::CategoriesDialog)
{
  ui->setupUi(this);
  fillTable();
  connect(ui->categoriesTable, SIGNAL(cellChanged(int, int)), this,
          SLOT(cellChanged(int, int)));
  if (Settings::instance().nexus().categoryMappings()) {
    connect(ui->nexusRefresh, SIGNAL(clicked()), this, SLOT(nexusRefresh_clicked()));
    connect(ui->nexusImportButton, SIGNAL(clicked()), this,
            SLOT(nexusImport_clicked()));
    ui->nexusCategoryList->setDisabled(false);
  } else {
    ui->nexusCategoryList->setDisabled(true);
  }
}

CategoriesDialog::~CategoriesDialog()
{
  delete ui;
}

int CategoriesDialog::exec()
{
  GeometrySaver const gs(Settings::instance(), this);
  return QDialog::exec();
}

void CategoriesDialog::cellChanged(int row, int)
{
  int const currentID = ui->categoriesTable->item(row, 0)->text().toInt();
  if (currentID > m_HighestID &&
      currentID < CategoryAssignmentPolicy::ImportedCategoryIdBase) {
    m_HighestID = currentID;
  }
  m_IDs.insert(currentID);
}

bool CategoriesDialog::commitChanges()
{
  CategoryFactory& categories = CategoryFactory::instance();
  const int durableHighWater =
      Settings::instance().categoryLocalIdHighWater();
  for (int row = 0; row < ui->categoriesTable->rowCount(); ++row) {
    const auto* idItem = ui->categoriesTable->item(row, 0);
    bool originalOk    = false;
    const int original = idItem->data(Qt::UserRole).toInt(&originalOk);
    const int requested = idItem->text().toInt();
    if (!CategoryAssignmentPolicy::mayAssignCategoryId(
            durableHighWater, requested,
            originalOk ? std::optional<int>(original) : std::nullopt)) {
      MessageDialog::showMessage(
          tr("Category ID %1 is retired or reserved and cannot be reused.")
              .arg(requested),
          this);
      return false;
    }
  }
  if (!Settings::instance().advanceCategoryLocalIdHighWater(m_HighestID)) {
    MessageDialog::showMessage(
        tr("Category IDs could not be reserved. The changes were not saved."),
        this);
    return false;
  }
  const auto previousCategories = categories.m_Categories;
  const auto previousNexusMap   = categories.m_NexusMap;
  const auto previousIDMap      = categories.m_IDMap;
  categories.reset();

  for (int i = 0; i < ui->categoriesTable->rowCount(); ++i) {
    int const index = ui->categoriesTable->verticalHeader()->logicalIndex(i);
    QVariantList nexusData;
    if (ui->categoriesTable->item(index, 3) != nullptr)
      nexusData = ui->categoriesTable->item(index, 3)->data(Qt::UserRole).toList();
    std::vector<CategoryFactory::NexusCategory> nexusCats;
    for (const auto& nexusCat : nexusData) {
      nexusCats.emplace_back(
          nexusCat.toList()[0].toString(), nexusCat.toList()[1].toInt());
    }

    categories.addCategory(ui->categoriesTable->item(index, 0)->text().toInt(),
                           ui->categoriesTable->item(index, 1)->text(), nexusCats,
                           ui->categoriesTable->item(index, 2)->text().toInt());
  }

  categories.setParents();

  std::vector<CategoryFactory::NexusCategory> nexusCats;
  for (int i = 0; i < ui->nexusCategoryList->count(); ++i) {
    nexusCats.emplace_back(
        ui->nexusCategoryList->item(i)->data(Qt::DisplayRole).toString(),
        ui->nexusCategoryList->item(i)->data(Qt::UserRole).toInt());
  }

  categories.setNexusCategories(nexusCats);

  if (categories.saveCategories()) {
    return true;
  }

  categories.m_Categories = previousCategories;
  categories.m_NexusMap   = previousNexusMap;
  categories.m_IDMap      = previousIDMap;
  return false;
}

std::optional<int> CategoriesDialog::allocateCategoryID()
{
  const auto id = CategoryAssignmentPolicy::nextLocalCategoryId(
      std::max(m_HighestID,
               Settings::instance().categoryLocalIdHighWater()),
      m_IDs);
  if (!id) {
    MessageDialog::showMessage(
        tr("No category IDs remain in the local category namespace."), this);
    return std::nullopt;
  }
  if (!Settings::instance().advanceCategoryLocalIdHighWater(*id)) {
    MessageDialog::showMessage(
        tr("The new category ID could not be saved. No category was added."),
        this);
    return std::nullopt;
  }
  m_HighestID = *id;
  m_IDs.insert(*id);
  return id;
}

void CategoriesDialog::refreshIDs()
{
  m_HighestID = Settings::instance().categoryLocalIdHighWater();
  m_IDs.clear();
  for (int i = 0; i < ui->categoriesTable->rowCount(); ++i) {
    int const id = ui->categoriesTable->item(i, 0)->text().toInt();
    if (id > m_HighestID &&
        id < CategoryAssignmentPolicy::ImportedCategoryIdBase) {
      m_HighestID = id;
    }
    m_IDs.insert(id);
  }
}

void CategoriesDialog::fillTable()
{
  CategoryFactory const& categories = CategoryFactory::instance();
  QTableWidget* table         = ui->categoriesTable;
  QListWidget* list           = ui->nexusCategoryList;

  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  table->verticalHeader()->setVisible(false);
  table->setItemDelegateForColumn(
      0, new ValidatingDelegate(
             this, new NewIDValidator(m_IDs, m_HighestID)));
  table->setItemDelegateForColumn(
      2, new ValidatingDelegate(this, new ExistingIDValidator(m_IDs)));
  table->setItemDelegateForColumn(
      3, new ValidatingDelegate(this,
                                new QRegularExpressionValidator(
                                    QRegularExpression("([0-9]+)?(,[0-9]+)*"), this)));

  int row = 0;
  for (const auto& category : categories.m_Categories) {
    if (category.ID() == 0) {
      --row;
      continue;
    }
    ++row;
    table->insertRow(row);

    auto idItem = std::make_unique<QTableWidgetItem>();
    idItem->setData(Qt::DisplayRole, category.ID());
    idItem->setData(Qt::UserRole, category.ID());

    auto nameItem = std::make_unique<QTableWidgetItem>(category.name());
    auto parentIDItem = std::make_unique<QTableWidgetItem>();
    parentIDItem->setData(Qt::DisplayRole, category.parentID());
    auto nexusCatItem = std::make_unique<QTableWidgetItem>();

    table->setItem(row, 0, idItem.release());
    table->setItem(row, 1, nameItem.release());
    table->setItem(row, 2, parentIDItem.release());
    table->setItem(row, 3, nexusCatItem.release());
  }

  for (const auto& nexusCat : categories.m_NexusMap) {
    auto nexusItem = std::make_unique<QListWidgetItem>();
    nexusItem->setData(Qt::DisplayRole, nexusCat.second.name());
    nexusItem->setData(Qt::UserRole, nexusCat.second.ID());
    list->addItem(nexusItem.release());
    auto *item = table->item(categories.resolveNexusID(nexusCat.first) - 1, 3);
    if (item != nullptr) {
      auto itemData = item->data(Qt::UserRole).toList();
      QVariantList newData;
      newData.append(nexusCat.second.name());
      newData.append(nexusCat.second.ID());
      itemData.insert(itemData.length(), newData);
      QStringList names;
      for (const auto& cat : itemData) {
        names.append(cat.toList()[0].toString());
      }
      item->setData(Qt::UserRole, itemData);
      item->setData(Qt::DisplayRole, names.join(", "));
    }
  }

  table->setSortingEnabled(true);
  refreshIDs();
}

void CategoriesDialog::addCategory_clicked()
{
  const auto categoryID = allocateCategoryID();
  if (!categoryID) {
    return;
  }
  int const row = m_ContextRow >= 0 ? m_ContextRow : 0;
  ui->categoriesTable->setSortingEnabled(false);
  ui->categoriesTable->insertRow(row);

  auto idItem = std::make_unique<QTableWidgetItem>();
  idItem->setData(Qt::DisplayRole, *categoryID);
  idItem->setData(Qt::UserRole, *categoryID);
  auto parentIDItem = std::make_unique<QTableWidgetItem>();
  parentIDItem->setData(Qt::DisplayRole, 0);

  ui->categoriesTable->setItem(row, 0, idItem.release());
  ui->categoriesTable->setItem(row, 1, new QTableWidgetItem("new"));
  ui->categoriesTable->setItem(row, 2, parentIDItem.release());
  ui->categoriesTable->setItem(row, 3, new QTableWidgetItem(""));
  ui->categoriesTable->setSortingEnabled(true);
}

void CategoriesDialog::removeCategory_clicked()
{
  if (m_ContextRow >= 0)
    ui->categoriesTable->removeRow(m_ContextRow);
}

void CategoriesDialog::removeNexusMap_clicked()
{
  if (m_ContextRow >= 0) {
    ui->categoriesTable->item(m_ContextRow, 3)->setData(Qt::UserRole, QVariantList());
    ui->categoriesTable->item(m_ContextRow, 3)->setData(Qt::DisplayRole, QString());
  }
}

void CategoriesDialog::nexusRefresh_clicked()
{
  CategoryFactory::instance().refreshNexusCategories(this);
}

void CategoriesDialog::nexusImport_clicked()
{
  auto importDialog = CategoryImportDialog(this);
  if (importDialog.exec() && importDialog.strategy()) {
    refreshIDs();
    QTableWidget* table = ui->categoriesTable;
    QListWidget* list   = ui->nexusCategoryList;
    if (importDialog.strategy() == CategoryImportDialog::Overwrite) {
      table->setRowCount(0);
      m_HighestID = 0;
    }
    int row = 0;
    table->setSortingEnabled(false);
    for (int i = 0; i < list->count(); ++i) {
      QString const name = list->item(i)->data(Qt::DisplayRole).toString();
      int const nexusID  = list->item(i)->data(Qt::UserRole).toInt();
      QStringList nexusLabel;
      QVariantList nexusData;
      nexusLabel.append(name);
      QVariantList data;
      data.append(QVariant(name));
      data.append(QVariant(nexusID));
      nexusData.insert(nexusData.size(), data);
      auto nexusCatItem = std::make_unique<QTableWidgetItem>(nexusLabel.join(", "));
      nexusCatItem->setData(Qt::UserRole, nexusData);
      if (table->findItems(name, Qt::MatchExactly).empty()) {
        const auto categoryID = allocateCategoryID();
        if (!categoryID) {
          break;
        }
        row = table->rowCount();
        table->insertRow(table->rowCount());

        auto idItem = std::make_unique<QTableWidgetItem>();
        idItem->setData(Qt::DisplayRole, *categoryID);
        idItem->setData(Qt::UserRole, *categoryID);

        auto nameItem = std::make_unique<QTableWidgetItem>(name);
        auto parentIDItem = std::make_unique<QTableWidgetItem>();
        parentIDItem->setData(Qt::DisplayRole, 0);  // No parent

        table->setItem(row, 0, idItem.release());
        table->setItem(row, 1, nameItem.release());
        table->setItem(row, 2, parentIDItem.release());

        if (importDialog.assign()) {
          table->setItem(row, 3, nexusCatItem.release());
        }
      } else {
        for (auto *item : table->findItems(name, Qt::MatchContains | Qt::MatchWrap)) {
          if (item->column() == 1 && item->text() == name && importDialog.remap()) {
            table->setItem(item->row(), 3, nexusCatItem.release());
          } else if (importDialog.remap()) {
            auto blankItem = std::make_unique<QTableWidgetItem>();
            blankItem->setData(Qt::UserRole, QVariantList());
            table->setItem(item->row(), 3, blankItem.release());
          }
        }
      }
    }
    table->setSortingEnabled(true);
    refreshIDs();
  }
}

void CategoriesDialog::nxmGameInfoAvailable(QString gameName, QVariant,
                                            QVariant resultData, int)
{
  QVariantMap result          = resultData.toMap();
  QVariantList const categories     = result["categories"].toList();
  CategoryFactory const& catFactory = CategoryFactory::instance();
  QListWidget* list           = ui->nexusCategoryList;
  list->clear();
  for (const auto& category : categories) {
    auto catMap = category.toMap();
    auto nexusItem = std::make_unique<QListWidgetItem>();
    nexusItem->setData(Qt::DisplayRole, catMap["name"].toString());
    nexusItem->setData(Qt::UserRole, catMap["category_id"].toInt());
    list->addItem(nexusItem.release());
  }
}

void CategoriesDialog::nxmRequestFailed(QString, int, int, QVariant, int, int errorCode,
                                        const QString& errorMessage)
{
  MessageDialog::showMessage(
      tr("Error %1: Request to Nexus failed: %2").arg(errorCode).arg(errorMessage),
      this);
}

void CategoriesDialog::on_categoriesTable_customContextMenuRequested(const QPoint& pos)
{
  m_ContextRow = ui->categoriesTable->rowAt(pos.y());
  QMenu menu;
  menu.addAction(tr("Add"), this, SLOT(addCategory_clicked()));
  menu.addAction(tr("Remove"), this, SLOT(removeCategory_clicked()));
  if (Settings::instance().nexus().categoryMappings()) {
    menu.addAction(tr("Remove Nexus Mapping(s)"), this, SLOT(removeNexusMap_clicked()));
  }

  menu.exec(ui->categoriesTable->mapToGlobal(pos));
}

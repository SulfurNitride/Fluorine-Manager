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

#include "archivetree.h"

#include <QDebug>
#include <QDragMoveEvent>
#include <QMessageBox>

#include <uibase/ifiletree.h>
#include <uibase/log.h>
#include <uibase/report.h>

using namespace MOBase;

// Implementation details for the ArchiveTree widget:
//
// The ArchiveTreeWidget presents to the user the underlying IFileTree, but in order
// to increase performance, the tree is populated dynamically when required. Populating
// the tree is currently required:
//   1) when a branch of the tree widget is expanded,
//   2) when an item is moved to a tree,
//   3) when a directory is created,
//   4) when a directory is "set as data root".
//
// Case 1 is handled automatically in the setExpanded method of ArchiveTreeWidget. Cases
// 2 and 3 could be dealt with differently, but populating the tree before inserting an
// item makes everything else easier (not that populating the widget is different from
// populating the IFileTree which is done automatically). Case 4 is handled manually in
// setDataRoot.
//
// Another specificity of the implementation is the treeCheckStateChanged() signal
// emitted by the ArchiveTreeWidget. This signal is used to avoid having to connect to
// the itemChanged() signal or overriding the dataChanged() method which are called much
// more often than those. The treeCheckStateChanged() signal is send only for the item
// that has actually been changed by the user. While the interface is automatically
// updated by Qt, we need to update the underlying tree manually. This is done by doing
// the following things:
//   1) When an item is unchecked:
//      - We detach the corresponding entry from its parent, and recursively detach the
//      empty
//        parents (or the ones that become empty).
//      - If the entry is a directory and the item has been populated, we recursively
//      detach
//        all the child entries for all the child items that have been populated (no
//        need to do it for non-populated items)>
//   2) When an item is checked, we do the same process but we re-attach parents and
//   re-insert
//      children.
//
// Detaching or re-attaching parents is also done when a directory is created (if the
// directory is created in an empty directory, we need to re-attach), or when an item is
// moved (if the directory the item comes from is now empty or if the target directory
// was empty).
//

ArchiveTreeWidgetItem::ArchiveTreeWidgetItem(QString dataName)
    : QTreeWidgetItem(QStringList(dataName)), m_Entry(nullptr)
{
  setFlags(flags() & ~Qt::ItemIsUserCheckable);
  setExpanded(true);
  m_Populated = true;
}

ArchiveTreeWidgetItem::ArchiveTreeWidgetItem(
    std::shared_ptr<MOBase::FileTreeEntry> entry)
    : QTreeWidgetItem(QStringList(entry->name())), m_Entry(entry)
{
  if (entry->isDir()) {
    setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    setFlags(flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
  } else {
    setFlags(flags() | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren);
  }
  setCheckState(0, Qt::Checked);
  setToolTip(0, entry->path());
}

void ArchiveTreeWidgetItem::setData(int column, int role, const QVariant& value)
{
  ArchiveTreeWidget* tree = static_cast<ArchiveTreeWidget*>(treeWidget());
  if (tree != nullptr && tree->m_Emitter == nullptr) {
    tree->m_Emitter = this;
  }
  QTreeWidgetItem::setData(column, role, value);
  if (tree != nullptr && tree->m_Emitter == this) {
    tree->m_Emitter = nullptr;
    if (role == Qt::CheckStateRole) {
      tree->onTreeCheckStateChanged(this);
    }
  }
}

void ArchiveTreeWidgetItem::populate(bool force)
{

  // Only populates once:
  if (isPopulated() && !force) {
    return;
  }

  // Should never happen:
  if (entry()->isFile()) {
    return;
  }

  // We go in reverse of the tree because we want to insert the original
  // entries at the beginning (the item can only contains children if a
  // directory has been created under it or if entries has been moved under
  // it):
  for (auto& entry : *entry()->astree()) {
    auto newItem = new ArchiveTreeWidgetItem(entry);
    newItem->setCheckState(0, flags().testFlag(Qt::ItemIsUserCheckable) ? checkState(0)
                                                                        : Qt::Checked);
    addChild(newItem);
  }

  // If the item is unchecked, we need to clear it because it has not been cleared
  // before:
  if (flags().testFlag(Qt::ItemIsUserCheckable) && checkState(0) == Qt::Unchecked) {
    entry()->astree()->clear();
  }

  m_Populated = true;
}

ArchiveTreeWidget::ArchiveTreeWidget(QWidget* parent) : QTreeWidget(parent)
{
  setAutoExpandDelay(1000);
  setDragDropOverwriteMode(true);
  connect(this, &ArchiveTreeWidget::itemExpanded, this,
          &ArchiveTreeWidget::populateItem);
}

void ArchiveTreeWidget::setup(QString dataFolderName)
{
  m_ViewRoot = new ArchiveTreeWidgetItem("<" + dataFolderName + ">");
  m_DataRoot = nullptr;
  addTopLevelItem(m_ViewRoot);
}

void ArchiveTreeWidget::populateItem(QTreeWidgetItem* item)
{
  static_cast<ArchiveTreeWidgetItem*>(item)->populate();
}

void ArchiveTreeWidget::setDataRoot(ArchiveTreeWidgetItem* const root)
{
  if (root != m_DataRoot) {
    if (m_DataRoot != nullptr) {
      m_DataRoot->addChildren(m_ViewRoot->takeChildren());
    }

    // Force populate:
    root->populate();

    m_DataRoot = root;
    m_ViewRoot->setEntry(m_DataRoot->entry());
    m_ViewRoot->addChildren(m_DataRoot->takeChildren());
    m_ViewRoot->setExpanded(true);
  }

  emit treeChanged();
}

void ArchiveTreeWidget::detachParents(ArchiveTreeWidgetItem* item)
{
  auto entry  = item->entry();
  auto parent = entry->parent();
  entry->detach();
  while (parent != nullptr && parent->empty()) {
    auto tmp = parent->parent();
    parent->detach();
    parent = tmp;
  }
}

void ArchiveTreeWidget::attachParents(ArchiveTreeWidgetItem* item)
{
  while (item->parent() != nullptr) {
    auto parent      = static_cast<ArchiveTreeWidgetItem*>(item->parent());
    auto parentEntry = parent->entry();
    if (parentEntry != nullptr) {
      parentEntry->astree()->insert(item->entry());
    }
    item = parent;
  }
}

void ArchiveTreeWidget::recursiveInsert(ArchiveTreeWidgetItem* item)
{
  if (item->isPopulated()) {
    auto tree = item->entry()->astree();
    for (int i = 0; i < item->childCount(); ++i) {
      auto child = static_cast<ArchiveTreeWidgetItem*>(item->child(i));
      tree->insert(child->entry());
      if (child->entry()->isDir()) {
        recursiveInsert(child);
      }
    }
  }
}

void ArchiveTreeWidget::recursiveDetach(ArchiveTreeWidgetItem* item)
{
  if (item->isPopulated()) {
    for (int i = 0; i < item->childCount(); ++i) {
      auto child = static_cast<ArchiveTreeWidgetItem*>(item->child(i));
      if (child->entry()->isDir()) {
        recursiveDetach(child);
      }
    }
    item->entry()->astree()->clear();
  }
}

ArchiveTreeWidgetItem* ArchiveTreeWidget::addDirectory(ArchiveTreeWidgetItem* item,
                                                       QString name)
{
  auto tree     = item->entry()->astree();
  auto* newItem = new ArchiveTreeWidgetItem(tree->addDirectory(name));

  // find the insert position
  auto it   = std::find_if(tree->begin(), tree->end(), [name](auto&& entry) {
    return entry->compare(name) == 0;
  });
  int index = it - tree->begin();
  MOBase::log::debug("insert at: {}", index);
  item->insertChild(index, newItem);

  newItem->setCheckState(0, Qt::Checked);
  attachParents(item);
  emit treeChanged();

  return newItem;
}

void ArchiveTreeWidget::moveItem(ArchiveTreeWidgetItem* source,
                                 ArchiveTreeWidgetItem* target)
{
  // just insert the source in the target.
  auto tree = target->entry()->astree();

  detachParents(source);

  // check if an entry exists with the same name, we check
  // in the tree widget to find unchecked items
  for (int i = 0; i < target->childCount(); ++i) {
    auto* child = target->child(i);
    if (child->entry()->compare(source->entry()->name()) == 0) {
      // remove existing file and force check existing directory
      if (child->entry()->isFile()) {
        target->removeChild(child);
      } else {
        child->setCheckState(0, Qt::Checked);
      }
      break;
    }
  }

  tree->insert(source->entry(), IFileTree::InsertPolicy::MERGE);

  attachParents(target);

  emit treeChanged();
}

void ArchiveTreeWidget::onTreeCheckStateChanged(ArchiveTreeWidgetItem* item)
{

  auto entry = item->entry();

  // If the entry is a directory, we need to either detach or re-attach all the
  // children. It is not possible to only detach the directory because if the
  // user uncheck a directory and then check a file under it, the other files would
  // still be attached.
  //
  // The two recursive methods only go down to the expanded (based on isPopulated()
  // tree, for two reasons:
  //   1. If a tree item has not been populated, then detaching an entry from its parent
  //   will
  //      delete it since there would be no remaining shared pointers.
  //   2. If the tree has not been populated yet, all the entries under it are still
  //   attached,
  //      so there is no need to process them differently. Detaching a non-expanded item
  //      can be done by simply detaching the tree, no need to detach all the children.
  if (entry->isDir()) {
    if (item->checkState(0) == Qt::Checked && item->isPopulated()) {
      recursiveInsert(item);
    } else if (item->checkState(0) == Qt::Unchecked && item->isPopulated()) {
      recursiveDetach(item);
    }
  }

  // Unchecked: we go up the parent chain removing all trees that are now empty:
  if (item->checkState(0) == Qt::Unchecked) {
    detachParents(item);
  }
  // Otherwize, we need to-reattach the parent:
  else {
    attachParents(item);
  }

  emit treeChanged();
}

bool ArchiveTreeWidget::testMovePossible(ArchiveTreeWidgetItem* source,
                                         ArchiveTreeWidgetItem* target)
{
  if (target == nullptr || source == nullptr) {
    return false;
  }

  if (target->flags().testFlag(Qt::ItemNeverHasChildren)) {
    return false;
  }

  if (source == target || source->parent() == target) {
    return false;
  }

  return true;
}

void ArchiveTreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
  QTreeWidgetItem* source = this->currentItem();
  if ((source == nullptr) || (source->parent() == nullptr)) {
    // can't change top level
    event->ignore();
    return;
  } else {
    QTreeWidget::dragEnterEvent(event);
  }
}

void ArchiveTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
  if (!testMovePossible(
          static_cast<ArchiveTreeWidgetItem*>(currentItem()),
          static_cast<ArchiveTreeWidgetItem*>(itemAt(event->position().toPoint())))) {
    event->ignore();
  } else {
    QTreeWidget::dragMoveEvent(event);
  }
}

static bool isAncestor(const QTreeWidgetItem* ancestor, const QTreeWidgetItem* item)
{
  QTreeWidgetItem* iter = item->parent();
  while (iter != nullptr) {
    if (iter == ancestor) {
      return true;
    }
    iter = iter->parent();
  }
  return false;
}

void ArchiveTreeWidget::refreshItem(ArchiveTreeWidgetItem* item)
{
  if (!item->isPopulated() || item->flags().testFlag(Qt::ItemNeverHasChildren)) {
    return;
  }

  // at this point, all child items are checked for we only remember the ones
  // that were expanded to re-expand them
  std::map<QString, bool, MOBase::FileNameComparator> expanded;
  while (item->childCount() > 0) {
    auto* child                      = item->child(0);
    expanded[child->entry()->name()] = child->isExpanded();
    item->removeChild(child);
  }

  item->populate(true);

  for (int i = 0; i < item->childCount(); ++i) {
    auto* child = item->child(i);
    if (expanded[child->entry()->name()]) {
      child->setExpanded(true);
    }
  }
}

void ArchiveTreeWidget::dropEvent(QDropEvent* event)
{
  event->ignore();

  // target widget (should be a directory)
  auto* target =
      static_cast<ArchiveTreeWidgetItem*>(itemAt(event->position().toPoint()));

  // this should not really happen because it is prevent by dragMoveEvent
  if (target->flags().testFlag(Qt::ItemNeverHasChildren)) {

    // this should really not happen, how should a file get to the top level?
    if (target->parent() == nullptr) {
      return;
    }

    target = target->parent();
  }

  // populate target if required
  target->populate();

  auto sourceItems = this->selectedItems();

  // check the selected items - we do not want to move only
  // some items so we check everything first and then move
  for (auto* source : sourceItems) {

    auto* aSource = static_cast<ArchiveTreeWidgetItem*>(source);

    // do not allow element to be dropped into one of its
    // own child
    if (isAncestor(source, target)) {
      event->accept();
      QMessageBox::warning(parentWidget(), tr("Cannot drop"),
                           tr("Cannot drop '%1' into one of its subfolder.")
                               .arg(aSource->entry()->name()));
      return;
    }

    auto sourceEntry = aSource->entry();
    auto targetEntry = target->entry()->astree()->find(sourceEntry->name());
    if (targetEntry && targetEntry->fileType() != sourceEntry->fileType()) {
      event->accept();
      QMessageBox::warning(parentWidget(), tr("Cannot drop"),
                           targetEntry->isFile()
                               ? tr("A file '%1' already exists in folder '%2'.")
                                     .arg(sourceEntry->name())
                                     .arg(target->entry()->name())
                               : tr("A folder '%1' already exists in folder '%2'.")
                                     .arg(sourceEntry->name())
                                     .arg(target->entry()->name()));
      return;
    }
  }

  for (auto* source : sourceItems) {

    auto* aSource = static_cast<ArchiveTreeWidgetItem*>(source);

    // this only check dropping an item on itself or dropping an item in
    // its parent so it is ok, it just does not do anything
    if (source->parent() == nullptr || !testMovePossible(aSource, target)) {
      continue;
    }

    // force expand item that are going to be merged
    for (int i = 0; i < target->childCount(); ++i) {
      auto* child = target->child(i);
      if (child->entry()->compare(aSource->entry()->name()) == 0 &&
          !child->flags().testFlag(Qt::ItemNeverHasChildren)) {
        child->setExpanded(true);
      }
    }

    // remove the source from its parent
    source->parent()->removeChild(source);

    // actually perform the move on the underlying tree model
    moveItem(aSource, target);
  }

  // refresh the target item - this assumes that itemMoved is called synchronously
  // and perform the FileTree changes
  refreshItem(target);
}

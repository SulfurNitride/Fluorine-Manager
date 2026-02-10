/*
Mod Organizer shared UI functionality

Copyright (C) 2013 Sebastian Herbord. All rights reserved.

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

#include <uibase/sortabletreewidget.h>
#include <QDropEvent>

namespace MOBase
{

SortableTreeWidget::SortableTreeWidget(QWidget* parent)
    : QTreeWidget(parent), m_LocalMoveOnly(false)
{}

void SortableTreeWidget::setLocalMoveOnly(bool localOnly)
{
  m_LocalMoveOnly = localOnly;
}

void SortableTreeWidget::dropEvent(QDropEvent* event)
{
  // only react on internal drag&drop
  if (event->source() == this) {
    QTreeWidget::dropEvent(event);
  }
}

Qt::DropActions SortableTreeWidget::supportedDropActions() const
{
  // we do our own drop handling, this ensures dropMimeData is called.
  return Qt::CopyAction;
}

bool SortableTreeWidget::dropMimeData(QTreeWidgetItem* parent, int index,
                                      const QMimeData*, Qt::DropAction)
{
  // TODO: ignores type of action set up in designer, always moves
  return this->moveSelection(parent, index);
}

bool SortableTreeWidget::moveSelection(QTreeWidgetItem* parent, int idx)
{
  QModelIndex parentIndex = indexFromItem(parent);
  std::vector<QPersistentModelIndex> persistentIndices;
  Q_FOREACH (const QModelIndex& index, selectedIndexes()) {
    if (index == parentIndex)
      return false;
    if (m_LocalMoveOnly && (parentIndex != index.parent()))
      return false;
    if (index.column() != 0)
      continue;
    persistentIndices.push_back(index);
  }

  int targetRow = -1;
  if (itemsExpandable() || !parentIndex.isValid()) {
    targetRow = model()->index(idx, 0, parentIndex).row();

    if (targetRow < 0) {
      targetRow = idx;
    }
  } else {
    // if items aren't expandable, we can't be placing items on sublevels
    targetRow   = parentIndex.row();
    parentIndex = QModelIndex();
  }

  // remove items from the list, store in temporary location
  QList<QTreeWidgetItem*> temp;
  for (auto iter = persistentIndices.rbegin(); iter != persistentIndices.rend();
       ++iter) {
    QTreeWidgetItem* item = itemFromIndex(*iter);
    if (item == nullptr)
      continue;
    if ((item == nullptr) || (item->parent() == nullptr)) {
      temp.append(takeTopLevelItem(iter->row()));
    } else {
      temp.append(item->parent()->takeChild(iter->row()));
    }
  }

  if (idx == -1) {
    // append
    if (parentIndex.isValid()) {
      parent->addChildren(temp);
    } else {
      addTopLevelItems(temp);
    }
  } else {
    if (parentIndex.isValid()) {
      parent->insertChildren(std::min<int>(targetRow, parent->childCount()), temp);
    } else {
      insertTopLevelItems(std::min<int>(targetRow, topLevelItemCount()), temp);
    }
  }
  emit itemsMoved();
  return true;
}

}  // namespace MOBase

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

#ifndef SORTABLETREEWIDGET_H
#define SORTABLETREEWIDGET_H

#include "dllimport.h"
#include <QTreeWidget>

namespace MOBase
{

class QDLLEXPORT SortableTreeWidget : public QTreeWidget
{
  Q_OBJECT

public:
  SortableTreeWidget(QWidget* parent = nullptr);

  /**
   * @brief sets whether sorting is allowed only within the branch of a tree
   * @param localOnly if true, objects can only be moved within a branch. if false
   * (default) they can be moved to a different branch
   */
  void setLocalMoveOnly(bool localOnly);
signals:
  /**
   * @brief signaled when items got moved. minimal atm
   */
  void itemsMoved();

protected:
  virtual void dropEvent(QDropEvent* event);
  virtual bool dropMimeData(QTreeWidgetItem* parent, int index, const QMimeData* data,
                            Qt::DropAction action);
  virtual Qt::DropActions supportedDropActions() const;

private:
  bool moveSelection(QTreeWidgetItem* parent, int index);

private:
  bool m_LocalMoveOnly;
};

}  // namespace MOBase

#endif  // SORTABLETREEWIDGET_H

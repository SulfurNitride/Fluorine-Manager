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

#ifndef ARCHIVETREE_H
#define ARCHIVETREE_H

#include <QTreeWidget>

#include <uibase/ifiletree.h>

class ArchiveTreeWidget;

// custom tree widget that holds a shared pointer to the file tree entry
// they represent
//
class ArchiveTreeWidgetItem : public QTreeWidgetItem
{
public:
  ArchiveTreeWidgetItem(QString dataName);
  ArchiveTreeWidgetItem(std::shared_ptr<MOBase::FileTreeEntry> entry);

public:
  // populate this tree widget item if it has not been populated yet
  // or if force is true
  //
  void populate(bool force = false);

  // check if this item has already been populated
  //
  bool isPopulated() const { return m_Populated; }

  // replace the entry corresponding to this item
  //
  void setEntry(std::shared_ptr<MOBase::FileTreeEntry> entry) { m_Entry = entry; }

  // retrieve the entry corresponding to this item
  //
  std::shared_ptr<MOBase::FileTreeEntry> entry() const { return m_Entry; }

  // overriden method to avoid propagating dataChanged events
  //
  void setData(int column, int role, const QVariant& value) override;

  ArchiveTreeWidgetItem* parent() const
  {
    return static_cast<ArchiveTreeWidgetItem*>(QTreeWidgetItem::parent());
  }

  ArchiveTreeWidgetItem* child(int index) const
  {
    return static_cast<ArchiveTreeWidgetItem*>(QTreeWidgetItem::child(index));
  }

protected:
  std::shared_ptr<MOBase::FileTreeEntry> m_Entry;
  bool m_Populated = false;

  friend class ArchiveTreeWidget;
};

// Qt tree widget used to display the content of an archive in the manual installation
// dialog
class ArchiveTreeWidget : public QTreeWidget
{

  Q_OBJECT

public:
  explicit ArchiveTreeWidget(QWidget* parent = 0);
  void setup(QString dataFolderName);

public:
  // set the data root widget
  //
  void setDataRoot(ArchiveTreeWidgetItem* const root);

  // create a directory under the given tree item, without
  // performing any check
  //
  ArchiveTreeWidgetItem* addDirectory(ArchiveTreeWidgetItem* treeItem, QString name);

  // return the root of the tree (the item corresponding to <data>)
  //
  ArchiveTreeWidgetItem* root() const { return m_ViewRoot; }

signals:

  // emitted when the tree has been modified
  //
  void treeChanged();

public slots:

protected:
  // detach the entry of this item from its parent, and recursively detach
  // all of its parent if they become
  //
  void detachParents(ArchiveTreeWidgetItem* item);

  // re-attach the entry of this item to its parent, and recursively attach
  // all of its parent if they were empty (and thus detached)
  //
  void attachParents(ArchiveTreeWidgetItem* item);

  // recursively re-insert all the entries below the given item in their
  // corresponding parents
  //
  // this method does not recurse in items that have not been populated yet
  //
  void recursiveInsert(ArchiveTreeWidgetItem* item);

  // recursively detach all the entries below the given item from their
  // corresponding parents
  //
  // this method does not recurse in items that have not been populated yet
  //
  void recursiveDetach(ArchiveTreeWidgetItem* item);

  // slot that trigger the given item to be populated if it has not already
  // been
  //
  void populateItem(QTreeWidgetItem* item);

  // move the source under the target
  //
  void moveItem(ArchiveTreeWidgetItem* source, ArchiveTreeWidgetItem* target);

  // called when the state of the item changed - unlike the standard QTreeWidget,
  // this is only called for the actual item, not its parent/children
  //
  void onTreeCheckStateChanged(ArchiveTreeWidgetItem* item);

  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  bool testMovePossible(ArchiveTreeWidgetItem* source, ArchiveTreeWidgetItem* target);

  // refresh the given item (after a drop)
  //
  void refreshItem(ArchiveTreeWidgetItem* item);

  // the widget item that emitted the dataChanged event
  ArchiveTreeWidgetItem* m_Emitter = nullptr;

  // IMPORTANT: if you intend to work on this and understand this, read the detailed
  // explanation at the beginning of the archivetree.cpp file
  //
  // - the data root is the real widget of the current data, this widget
  //   is not the real root that is added to the tree
  // - the view root is the actual tree in the widget (should be const but cannot be
  // since
  //   the parent tree cannot be consstructed in the member initializer list)
  //
  ArchiveTreeWidgetItem* m_DataRoot;
  ArchiveTreeWidgetItem* m_ViewRoot;

  friend class ArchiveTreeWidgetItem;
};

#endif  // ARCHIVETREE_H

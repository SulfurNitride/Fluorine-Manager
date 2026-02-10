
/*
    simplefiletreemodel.cpp

    Provides a simple tree model for files and folders
*/

#include "simplefiletreemodel.h"
#include "simplefiletreeitem.h"

#include <QDir>
#include <QFileIconProvider>
#include <QStringList>

SimpleFileTreeModel::SimpleFileTreeModel(const QStringList& data, QObject* parent)
    : QAbstractItemModel(parent)
{
  QFileIconProvider* provider = new QFileIconProvider();
  m_FolderIcon                = provider->icon(QFileIconProvider::Folder);
  m_FileIcon                  = provider->icon(QFileIconProvider::File);
  delete provider;
  m_RootItem = new SimpleFileTreeItem({tr("File Name")});
  setupModelData(data, m_RootItem);
}

SimpleFileTreeModel::~SimpleFileTreeModel()
{
  delete m_RootItem;
}

int SimpleFileTreeModel::columnCount(const QModelIndex& parent) const
{
  if (parent.isValid())
    return static_cast<SimpleFileTreeItem*>(parent.internalPointer())->columnCount();
  return m_RootItem->columnCount();
}

QVariant SimpleFileTreeModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid())
    return QVariant();

  SimpleFileTreeItem* item = static_cast<SimpleFileTreeItem*>(index.internalPointer());

  if (role == Qt::DecorationRole) {
    if (item->childCount() > 0) {
      return m_FolderIcon;
    } else {
      return m_FileIcon;
    }
  }

  if (role != Qt::DisplayRole)
    return QVariant();

  return item->data(index.column());
}

Qt::ItemFlags SimpleFileTreeModel::flags(const QModelIndex& index) const
{
  if (!index.isValid())
    return Qt::NoItemFlags;

  return QAbstractItemModel::flags(index);
}

QVariant SimpleFileTreeModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    return m_RootItem->data(section);

  return QVariant();
}

QModelIndex SimpleFileTreeModel::index(int row, int column,
                                       const QModelIndex& parent) const
{
  if (!hasIndex(row, column, parent))
    return QModelIndex();

  SimpleFileTreeItem* parentItem;

  if (!parent.isValid())
    parentItem = m_RootItem;
  else
    parentItem = static_cast<SimpleFileTreeItem*>(parent.internalPointer());

  SimpleFileTreeItem* childItem = parentItem->child(row);
  if (childItem)
    return createIndex(row, column, childItem);
  return QModelIndex();
}

QModelIndex SimpleFileTreeModel::parent(const QModelIndex& index) const
{
  if (!index.isValid())
    return QModelIndex();

  SimpleFileTreeItem* childItem =
      static_cast<SimpleFileTreeItem*>(index.internalPointer());
  SimpleFileTreeItem* parentItem = childItem->parentItem();

  if (parentItem == m_RootItem)
    return QModelIndex();

  return createIndex(parentItem->row(), 0, parentItem);
}

int SimpleFileTreeModel::rowCount(const QModelIndex& parent) const
{
  SimpleFileTreeItem* parentItem;
  if (parent.column() > 0)
    return 0;

  if (!parent.isValid())
    parentItem = m_RootItem;
  else
    parentItem = static_cast<SimpleFileTreeItem*>(parent.internalPointer());

  return parentItem->childCount();
}

void SimpleFileTreeModel::setupModelData(const QStringList& lines, SimpleFileTreeItem*)
{
  for (QString line : lines) {
    auto fullPath           = QDir::cleanPath(line);
    QStringList lineEntries = fullPath.split("/");
    auto currentParent      = m_RootItem;

    for (int i = 0; i < lineEntries.count(); i++) {
      QString currentEntryName = lineEntries[i];

      // check if item was already added
      SimpleFileTreeItem* currentEntry = currentParent->childByName(currentEntryName);

      // add tree item if not found
      if (currentEntry == nullptr) {
        QVector<QVariant> columnData;
        columnData.reserve(m_ColumnCount);
        columnData << currentEntryName;
        currentEntry = new SimpleFileTreeItem(columnData, currentParent);
        currentParent->appendChild(currentEntryName, currentEntry);
      }

      // as we go deeper into the path
      currentParent = currentEntry;
    }
  }
}

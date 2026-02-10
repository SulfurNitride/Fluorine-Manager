

/*
    simplefiletreeitem.cpp

    A container for items of data supplied by the simple file tree model.
*/

#include "simplefiletreeitem.h"

SimpleFileTreeItem::SimpleFileTreeItem(const QVector<QVariant>& data,
                                       SimpleFileTreeItem* parent)
    : m_itemData(data), m_parentItem(parent)
{}

SimpleFileTreeItem::~SimpleFileTreeItem()
{
  qDeleteAll(m_childItems);
}

void SimpleFileTreeItem::appendChild(const QString& name, SimpleFileTreeItem* item)
{
  m_childItems.append(item);
  m_childItemsByName.insert(name, item);
}

SimpleFileTreeItem* SimpleFileTreeItem::child(int row)
{
  if (row < 0 || row >= m_childItems.size())
    return nullptr;
  return m_childItems.at(row);
}

SimpleFileTreeItem* SimpleFileTreeItem::childByName(const QString& name)
{
  return m_childItemsByName.value(name);
}

QVector<SimpleFileTreeItem*> SimpleFileTreeItem::children()
{
  return m_childItems;
}

int SimpleFileTreeItem::childCount() const
{
  return m_childItems.count();
}

int SimpleFileTreeItem::columnCount() const
{
  return m_itemData.count();
}

QVariant SimpleFileTreeItem::data(int column) const
{
  if (column < 0 || column >= m_itemData.size())
    return QVariant();
  return m_itemData.at(column);
}

SimpleFileTreeItem* SimpleFileTreeItem::parentItem()
{
  return m_parentItem;
}

int SimpleFileTreeItem::row() const
{
  if (m_parentItem)
    return m_parentItem->m_childItems.indexOf(const_cast<SimpleFileTreeItem*>(this));

  return 0;
}

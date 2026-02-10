#ifndef SIMPLEFILETREEMODEL_H
#define SIMPLEFILETREEMODEL_H

#include <QAbstractItemModel>
#include <QIcon>
#include <QLineEdit>
#include <QModelIndex>
#include <QVariant>

#include <uibase/filterwidget.h>

using namespace MOBase;

class SimpleFileTreeItem;

class SimpleFileTreeModel : public QAbstractItemModel
{
  Q_OBJECT

public:
  explicit SimpleFileTreeModel(const QStringList& data, QObject* parent = nullptr);
  ~SimpleFileTreeModel();

  void setFilterWidgetList(QAbstractItemView* list) { m_FilterWidget.setList(list); }
  void setFilterWidgetEdit(QLineEdit* edit) { m_FilterWidget.setEdit(edit); }

  QVariant data(const QModelIndex& index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  QModelIndex index(int row, int column,
                    const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex& index) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;

private:
  // takes a list of relative file paths assumed with the same base folder and generates
  // the filetree model
  void setupModelData(const QStringList& lines, SimpleFileTreeItem* parent);
  const int m_ColumnCount = 1;
  SimpleFileTreeItem* m_RootItem;
  FilterWidget m_FilterWidget;
  QIcon m_FileIcon;
  QIcon m_FolderIcon;
};

#endif  // SIMPLEFILETREEMODEL_H

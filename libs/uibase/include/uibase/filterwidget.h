#ifndef FILTERWIDGET_H
#define FILTERWIDGET_H

#include "dllimport.h"
#include <QAbstractItemView>
#include <QLineEdit>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QToolButton>

namespace MOBase
{

class EventFilter;
class FilterWidget;

class QDLLEXPORT FilterWidgetProxyModel : public QSortFilterProxyModel
{
  Q_OBJECT;

public:
  FilterWidgetProxyModel(FilterWidget& fw, QWidget* parent = nullptr);
  void refreshFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    endFilterChange();
#else
    invalidateFilter();
#endif
  }

protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
  FilterWidget& m_filter;

  bool columnMatches(int sourceRow, const QModelIndex& sourceParent, int c,
                     const QRegularExpression& what) const;
};

class QDLLEXPORT FilterWidget : public QObject
{
  Q_OBJECT;

public:
  struct Options
  {
    bool useRegex           = false;
    bool regexCaseSensitive = false;
    bool regexExtended      = false;
    bool scrollToSelection  = false;
  };

  using predFun = std::function<bool(const QRegularExpression& what)>;
  using sortFun = std::function<bool(const QModelIndex&, const QModelIndex&)>;

  FilterWidget();

  static void setOptions(const Options& o);
  static Options options();

  void setEdit(QLineEdit* edit);
  void setList(QAbstractItemView* list);
  void clear();
  void scrollToSelection();
  bool empty() const;

  void setUpdateDelay(bool b);
  bool hasUpdateDelay() const;

  void setUseSourceSort(bool b);
  bool useSourceSort() const;

  void setSortPredicate(sortFun f);
  const sortFun& sortPredicate() const;

  void setFilterColumn(int i);
  int filterColumn() const;

  void setFilteringEnabled(bool b);
  bool filteringEnabled() const;

  void setFilteredBorder(bool b);
  bool filteredBorder() const;

  FilterWidgetProxyModel* proxyModel();
  QAbstractItemModel* sourceModel();

  QModelIndex mapFromSource(const QModelIndex& index) const;
  QModelIndex mapToSource(const QModelIndex& index) const;
  QItemSelection mapSelectionFromSource(const QItemSelection& sel) const;
  QItemSelection mapSelectionToSource(const QItemSelection& sel) const;

  bool matches(predFun pred) const;
  bool matches(const QString& s) const;

signals:
  void aboutToChange(const QString& oldFilter, const QString& newFilter);
  void changed(const QString& oldFilter, const QString& newFilter);

private:
  using Compiled = QList<QList<QRegularExpression>>;

  QLineEdit* m_edit;
  QAbstractItemView* m_list;
  FilterWidgetProxyModel* m_proxy;
  EventFilter* m_eventFilter;
  QToolButton* m_clear;
  QString m_text;
  Compiled m_compiled;
  QTimer* m_timer;
  std::vector<QShortcut*> m_shortcuts;
  bool m_useDelay;
  bool m_valid;
  bool m_useSourceSort;
  sortFun m_lt;
  int m_filterColumn;
  bool m_filteringEnabled;
  bool m_filteredBorder;

  void hookEdit();
  void unhookEdit();

  void hookList();
  void setShortcuts();
  void unhookList();

  void createClear();
  void repositionClearButton();

  void onTextChanged();
  void onFind();
  void onReset();
  void onResized();
  void onContextMenu(QObject*, QContextMenuEvent* e);

  void set();
  void update();
  void compile();
};

}  // namespace MOBase

#endif  // FILTERWIDGET_H

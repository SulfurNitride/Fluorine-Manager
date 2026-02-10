#include <uibase/filterwidget.h>
#include <uibase/eventfilter.h>
#include <uibase/log.h>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QTimer>

namespace MOBase
{

void setStyleProperty(QWidget* w, const char* k, const QVariant& v)
{
  w->setProperty(k, v);

  if (auto* s = w->style()) {
    // changing properties doesn't repolish automatically
    s->unpolish(w);
    s->polish(w);
  }

  // the qss will probably change the border, which requires sending a manual
  // StyleChange because the box model has changed
  QEvent event(QEvent::StyleChange);
  QApplication::sendEvent(w, &event);
  w->update();
  w->updateGeometry();
}

FilterWidgetProxyModel::FilterWidgetProxyModel(FilterWidget& fw, QWidget* parent)
    : QSortFilterProxyModel(parent), m_filter(fw)
{
  setRecursiveFilteringEnabled(true);
}

bool FilterWidgetProxyModel::filterAcceptsRow(int sourceRow,
                                              const QModelIndex& sourceParent) const
{
  if (!m_filter.filteringEnabled()) {
    return true;
  }

  const auto cols = sourceModel()->columnCount();

  const auto m = m_filter.matches([&](auto&& regex) {
    if (m_filter.filterColumn() == -1) {
      for (int c = 0; c < cols; ++c) {
        if (columnMatches(sourceRow, sourceParent, c, regex)) {
          return true;
        }
      }

      return false;
    } else {
      return columnMatches(sourceRow, sourceParent, m_filter.filterColumn(), regex);
    }
  });

  return m;
}

bool FilterWidgetProxyModel::columnMatches(int sourceRow,
                                           const QModelIndex& sourceParent, int c,
                                           const QRegularExpression& regex) const
{
  QModelIndex index = sourceModel()->index(sourceRow, c, sourceParent);
  const auto text   = sourceModel()->data(index, Qt::DisplayRole).toString();

  return regex.match(text).hasMatch();
}

void FilterWidgetProxyModel::sort(int column, Qt::SortOrder order)
{
  if (m_filter.useSourceSort()) {
    // without this, the proxy seems to sometimes ignore the sorting done below,
    // no idea why
    QSortFilterProxyModel::sort(-1, order);

    sourceModel()->sort(column, order);
  } else {
    QSortFilterProxyModel::sort(column, order);
  }
}

bool FilterWidgetProxyModel::lessThan(const QModelIndex& left,
                                      const QModelIndex& right) const
{
  if (auto lt = m_filter.sortPredicate()) {
    return lt(left, right);
  } else {
    return QSortFilterProxyModel::lessThan(left, right);
  }
}

static FilterWidget::Options s_options;

FilterWidget::FilterWidget()
    : m_edit(nullptr), m_list(nullptr), m_proxy(nullptr), m_eventFilter(nullptr),
      m_clear(nullptr), m_timer(nullptr), m_useDelay(false), m_valid(true),
      m_useSourceSort(false), m_filterColumn(-1), m_filteringEnabled(true),
      m_filteredBorder(true)
{
  m_timer = new QTimer(this);
  m_timer->setSingleShot(true);
  QObject::connect(
      m_timer, &QTimer::timeout, this,
      [&] {
        set();
      },
      Qt::QueuedConnection);
}

void FilterWidget::setOptions(const Options& o)
{
  s_options = o;
}

FilterWidget::Options FilterWidget::options()
{
  return s_options;
}

void FilterWidget::setEdit(QLineEdit* edit)
{
  unhookEdit();

  m_edit = edit;

  if (m_edit) {
    m_edit->setPlaceholderText(QObject::tr("Filter"));

    createClear();
    hookEdit();
    clear();
  }
}

void FilterWidget::setList(QAbstractItemView* list)
{
  unhookList();

  m_list = list;

  if (list) {
    hookList();
  } else {
    m_proxy = nullptr;
  }
}

void FilterWidget::clear()
{
  if (!m_edit) {
    return;
  }

  m_edit->clear();
}

void FilterWidget::scrollToSelection()
{
  if (options().scrollToSelection && m_list &&
      m_list->selectionModel()->hasSelection()) {
    m_list->scrollTo(m_list->selectionModel()->selectedIndexes()[0]);
  }
}

bool FilterWidget::empty() const
{
  return m_text.isEmpty();
}

void FilterWidget::setUpdateDelay(bool b)
{
  m_useDelay = b;
}

bool FilterWidget::hasUpdateDelay() const
{
  return m_useDelay;
}

void FilterWidget::setUseSourceSort(bool b)
{
  m_useSourceSort = b;
}

bool FilterWidget::useSourceSort() const
{
  return m_useSourceSort;
}

void FilterWidget::setSortPredicate(sortFun f)
{
  m_lt = f;
}

const FilterWidget::sortFun& FilterWidget::sortPredicate() const
{
  return m_lt;
}

void FilterWidget::setFilterColumn(int i)
{
  m_filterColumn = i;
}

int FilterWidget::filterColumn() const
{
  return m_filterColumn;
}

void FilterWidget::setFilteringEnabled(bool b)
{
  m_filteringEnabled = b;
}

bool FilterWidget::filteringEnabled() const
{
  return m_filteringEnabled;
}

void FilterWidget::setFilteredBorder(bool b)
{
  m_filteredBorder = b;
}

bool FilterWidget::filteredBorder() const
{
  return m_filteredBorder;
}

FilterWidgetProxyModel* FilterWidget::proxyModel()
{
  return m_proxy;
}

QAbstractItemModel* FilterWidget::sourceModel()
{
  if (m_proxy) {
    return m_proxy->sourceModel();
  } else if (m_list) {
    return m_list->model();
  } else {
    return nullptr;
  }
}

QModelIndex FilterWidget::mapFromSource(const QModelIndex& index) const
{
  if (m_proxy) {
    return m_proxy->mapFromSource(index);
  } else {
    log::error("FilterWidget::mapFromSource() called, but proxy isn't set up");
    return index;
  }
}

QModelIndex FilterWidget::mapToSource(const QModelIndex& index) const
{
  if (m_proxy) {
    return m_proxy->mapToSource(index);
  } else {
    log::error("FilterWidget::mapToSource() called, but proxy isn't set up");
    return index;
  }
}

QItemSelection FilterWidget::mapSelectionFromSource(const QItemSelection& sel) const
{
  if (m_proxy) {
    return m_proxy->mapSelectionFromSource(sel);
  } else {
    log::error("FilterWidget::mapToSource() called, but proxy isn't set up");
    return sel;
  }
}

QItemSelection FilterWidget::mapSelectionToSource(const QItemSelection& sel) const
{
  if (m_proxy) {
    return m_proxy->mapSelectionToSource(sel);
  } else {
    log::error("FilterWidget::mapToSource() called, but proxy isn't set up");
    return sel;
  }
}

void FilterWidget::compile()
{
  Compiled compiled;

  if (s_options.useRegex) {
    QRegularExpression::PatternOptions flags =
        QRegularExpression::DotMatchesEverythingOption;

    if (!s_options.regexCaseSensitive) {
      flags |= QRegularExpression::CaseInsensitiveOption;
    }

    if (s_options.regexExtended) {
      flags |= QRegularExpression::ExtendedPatternSyntaxOption;
    }

    compiled.push_back({QRegularExpression(m_text, flags)});
  } else {
    const QStringList ORList = [&] {
      QString filterCopy = QString(m_text);
      filterCopy.replace("||", ";").replace("OR", ";").replace("|", ";");
      return filterCopy.split(";", Qt::SkipEmptyParts);
    }();

    // split in ORSegments that internally use AND logic
    for (const auto& ORSegment : ORList) {
      const auto keywords = ORSegment.split(" ", Qt::SkipEmptyParts);
      QList<QRegularExpression> regexes;

      for (const auto& keyword : keywords) {
        const QString escaped = QRegularExpression::escape(keyword);

        const auto flags = QRegularExpression::CaseInsensitiveOption |
                           QRegularExpression::DotMatchesEverythingOption;

        regexes.push_back(QRegularExpression(escaped, flags));
      }

      compiled.push_back(regexes);
    }
  }

  bool valid = true;

  for (auto&& ANDKeywords : compiled) {
    for (auto&& keyword : ANDKeywords) {
      if (!keyword.isValid()) {
        valid = false;
        break;
      }
    }

    if (!valid) {
      break;
    }
  }

  m_valid = valid;
  setStyleProperty(m_edit, "valid-filter", m_valid);

  if (m_valid) {
    m_compiled = std::move(compiled);
  }
}

bool FilterWidget::matches(predFun pred) const
{
  if (m_compiled.isEmpty() || !pred) {
    return true;
  }

  for (auto& ANDKeywords : m_compiled) {
    bool segmentGood = true;

    // check each word in the segment for match, each word needs to be matched
    // but it doesn't matter where.
    for (auto& currentKeyword : ANDKeywords) {
      if (!pred(currentKeyword)) {
        segmentGood = false;
      }
    }

    if (segmentGood) {
      // the last AND loop didn't break so the ORSegments is true so mod
      // matches filter
      return true;
    }
  }

  return false;
}

bool FilterWidget::matches(const QString& s) const
{
  return matches([&](const QRegularExpression& re) {
    return re.match(s).hasMatch();
  });
}

void FilterWidget::hookEdit()
{
  m_eventFilter = new EventFilter(m_edit, [&](auto* w, auto* e) {
    if (e->type() == QEvent::Resize) {
      onResized();
    } else if (e->type() == QEvent::ContextMenu) {
      onContextMenu(w, static_cast<QContextMenuEvent*>(e));
      return true;
    }

    return false;
  });

  m_edit->installEventFilter(m_eventFilter);
  QObject::connect(m_edit, &QLineEdit::textChanged, [&] {
    onTextChanged();
  });
}

void FilterWidget::unhookEdit()
{
  if (m_clear) {
    delete m_clear;
    m_clear = nullptr;
  }

  if (m_edit) {
    m_edit->removeEventFilter(m_eventFilter);
  }
}

void FilterWidget::hookList()
{
  m_proxy = new FilterWidgetProxyModel(*this);
  m_proxy->setSourceModel(m_list->model());
  m_list->setModel(m_proxy);

  setShortcuts();
}

void FilterWidget::unhookList()
{
  if (m_proxy && m_list) {
    auto* model = m_proxy->sourceModel();
    m_proxy->setSourceModel(nullptr);
    delete m_proxy;

    m_list->setModel(model);
  }

  for (auto* s : m_shortcuts) {
    delete s;
  }

  m_shortcuts.clear();
}

// walks up the parents of `w`, returns true if one of them is a QDialog
//
bool topLevelIsDialog(QWidget* w)
{
  if (!w) {
    return false;
  }

  auto* p = w->parentWidget();
  while (p) {
    if (dynamic_cast<QDialog*>(p)) {
      return true;
    }

    p = p->parentWidget();
  }

  return false;
}

void FilterWidget::setShortcuts()
{
  auto activate = [this] {
    onFind();
  };

  auto reset = [this] {
    onReset();
  };

  auto hookActivate = [&](auto* w) {
    auto* s = new QShortcut(QKeySequence::Find, w);
    s->setAutoRepeat(false);
    s->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(s, &QShortcut::activated, activate);
    m_shortcuts.push_back(s);
  };

  auto hookReset = [&](auto* w) {
    auto* s = new QShortcut(QKeySequence(Qt::Key_Escape), w);
    s->setAutoRepeat(false);
    s->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(s, &QShortcut::activated, reset);
    m_shortcuts.push_back(s);
  };

  // don't hook the escape key for reset when the filter is in a dialog, the
  // standard behaviour is to close the dialog
  const bool inDialog =
      topLevelIsDialog(m_list ? static_cast<QWidget*>(m_list) : m_edit);

  if (m_list) {
    hookActivate(m_list);

    if (!inDialog) {
      hookReset(m_list);
    }
  }

  if (m_edit) {
    hookActivate(m_edit);

    if (!inDialog) {
      hookReset(m_edit);
    }
  }
}

void FilterWidget::onFind()
{
  if (m_edit) {
    m_edit->setFocus();
    m_edit->selectAll();
  }
}

void FilterWidget::onReset()
{
  clear();

  if (m_list) {
    m_list->setFocus();
  }
}

void FilterWidget::createClear()
{
  m_clear = new QToolButton(m_edit);

  QPixmap pixmap(":/MO/gui/edit_clear");
  m_clear->setIcon(QIcon(pixmap));
  m_clear->setIconSize(pixmap.size());
  m_clear->setCursor(Qt::ArrowCursor);
  m_clear->setStyleSheet("QToolButton { border: none; padding: 0px; }");
  m_clear->hide();

  QObject::connect(m_clear, &QToolButton::clicked, [&] {
    clear();
  });

  repositionClearButton();
}

void FilterWidget::set()
{
  const QString old = m_text;

  QString currentText;
  if (m_edit != nullptr) {
    currentText = m_edit->text();
  } else {
    currentText = m_text;
  }

  emit aboutToChange(old, currentText);

  m_text = currentText;
  compile();

  if (m_proxy) {
    m_proxy->refreshFilter();
  }

  if (m_list) {
    if (m_filteredBorder) {
      setStyleProperty(m_list, "filtered", !m_text.isEmpty());
    }

    scrollToSelection();
  }

  emit changed(old, currentText);
}

void FilterWidget::update()
{
  if (!m_text.isEmpty()) {
    set();
  }
}

void FilterWidget::onTextChanged()
{
  m_clear->setVisible(!m_edit->text().isEmpty());

  const auto text = m_edit->text();
  if (text == m_text) {
    return;
  }

  if (m_useDelay) {
    m_timer->start(500);
  } else {
    set();
  }
}

void FilterWidget::onResized()
{
  repositionClearButton();
}

void FilterWidget::onContextMenu(QObject*, QContextMenuEvent* e)
{
  std::unique_ptr<QMenu> m(m_edit->createStandardContextMenu());
  m->setParent(m_edit);

  auto* title = new QAction(tr("Filter options"), m_edit);
  title->setEnabled(false);

  auto f = title->font();
  f.setBold(true);
  title->setFont(f);

  auto* regex = new QAction(tr("Use regular expressions"), m_edit);
  regex->setStatusTip(tr("Use regular expressions in filters"));
  regex->setCheckable(true);
  regex->setChecked(s_options.useRegex);

  connect(regex, &QAction::triggered, [&] {
    s_options.useRegex = regex->isChecked();
    update();
  });

  auto* cs = new QAction(tr("Case sensitive"), m_edit);
  //: leave "(/i)" verbatim
  cs->setStatusTip(tr("Make regular expressions case sensitive (/i)"));
  cs->setCheckable(true);
  cs->setChecked(s_options.regexCaseSensitive);
  cs->setEnabled(s_options.useRegex);

  connect(cs, &QAction::triggered, [&] {
    s_options.regexCaseSensitive = cs->isChecked();
    update();
  });

  auto* x = new QAction(tr("Extended"), m_edit);
  //: leave "(/x)" verbatim
  x->setStatusTip(tr("Ignores unescaped whitespace in regular expressions (/x)"));
  x->setCheckable(true);
  x->setChecked(s_options.regexExtended);
  x->setEnabled(s_options.useRegex);

  connect(x, &QAction::triggered, [&] {
    s_options.regexExtended = x->isChecked();
    update();
  });

  auto* sts = new QAction(tr("Keep selection in view"), m_edit);
  sts->setStatusTip(tr("Scroll to keep the current selection in view after filtering"));
  sts->setCheckable(true);
  sts->setChecked(s_options.scrollToSelection);

  connect(sts, &QAction::triggered, [&] {
    s_options.scrollToSelection = sts->isChecked();
    update();
  });

  m->insertSeparator(m->actions().first());
  m->insertAction(m->actions().first(), x);
  m->insertAction(m->actions().first(), cs);
  m->insertAction(m->actions().first(), regex);
  m->insertAction(m->actions().first(), sts);
  m->insertAction(m->actions().first(), title);

  m->exec(e->globalPos());
}

void FilterWidget::repositionClearButton()
{
  if (!m_clear) {
    return;
  }

  const QSize sz  = m_clear->sizeHint();
  const int frame = m_edit->style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
  const auto r    = m_edit->rect();

  const auto x = r.right() - frame - sz.width();
  const auto y = (r.bottom() + 1 - sz.height()) / 2;

  m_clear->move(x, y);
}

}  // namespace MOBase

#include <uibase/widgetutility.h>
#include <uibase/eventfilter.h>
#include <QCheckBox>
#include <QHeaderView>
#include <QMenu>
#include <QWidgetAction>

namespace MOBase
{

void onHeaderContextMenu(QTreeView* view, const QPoint& pos)
{
  auto* header = view->header();

  QMenu menu;

  // display a list of all headers as checkboxes
  QAbstractItemModel* model = header->model();

  for (int i = 1; i < model->columnCount(); ++i) {
    const QString columnName = model->headerData(i, Qt::Horizontal).toString();

    auto* checkBox = new QCheckBox(&menu);
    checkBox->setText(columnName);
    checkBox->setChecked(!header->isSectionHidden(i));

    // Enable the checkbox if 1) the section is visible, or 2) the
    // EnabledColumnRole is not found, or 3) the value for the role is true.
    auto display = model->headerData(i, Qt::Horizontal, EnabledColumnRole);
    checkBox->setEnabled(!header->isSectionHidden(i) || !display.isValid() ||
                         display.toBool());

    auto* checkableAction = new QWidgetAction(&menu);
    checkableAction->setDefaultWidget(checkBox);

    QObject::connect(checkBox, &QCheckBox::clicked, [=] {
      header->setSectionHidden(i, !checkBox->isChecked());
    });

    menu.addAction(checkableAction);
  }

  menu.exec(header->viewport()->mapToGlobal(pos));
}

void setCustomizableColumns(QTreeView* view)
{
  auto* header = view->header();

  header->setSectionsMovable(true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);

  QObject::connect(header, &QWidget::customContextMenuRequested, view,
                   [view](auto&& pos) {
                     onHeaderContextMenu(view, pos);
                   });
}

}  // namespace MOBase

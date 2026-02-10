#ifndef UIBASE_WIDGETUTILITY_INCLUDED
#define UIBASE_WIDGETUTILITY_INCLUDED

#include "dllimport.h"
#include <QAbstractItemView>
#include <QTreeView>

namespace MOBase
{

/**
 * Custom user-role that can be used in conjunction with `setCustomizableColumns`. If
 * a column has a value for this role, and the value is false, the checkbox
 * corresponding to the column will be disabled, otherwise the checkbox is enabled.
 */
constexpr auto EnabledColumnRole = Qt::UserRole + 1;

QDLLEXPORT void setCustomizableColumns(QTreeView* view);

}  // namespace MOBase

#endif  // UIBASE_WIDGETUTILITY_INCLUDED

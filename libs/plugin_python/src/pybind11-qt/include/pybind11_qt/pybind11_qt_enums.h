#ifndef PYTHON_PYBIND11_QT_ENUMS_HPP
#define PYTHON_PYBIND11_QT_ENUMS_HPP

#include <QColor>
#include <QMessageBox>

#include "details/pybind11_qt_enum.h"
#include "details/pybind11_qt_utils.h"

#define PYQT_ENUM(QPackage, QEnum)                                                     \
    namespace pybind11::detail {                                                       \
        namespace qt {                                                                 \
            template <>                                                                \
            struct EnumData<QEnum> {                                                   \
                constexpr static const auto package =                                  \
                    const_name("PyQt6.") + const_name(#QPackage);                      \
                constexpr static const auto name = qt_name_cpp2py(#QEnum);             \
            };                                                                         \
        }                                                                              \
        template <>                                                                    \
        struct type_caster<QEnum> : qt::qt_enum_caster<QEnum> {};                      \
    }

PYQT_ENUM(QtCore, Qt::AlignmentFlag);
PYQT_ENUM(QtCore, Qt::AnchorPoint);
PYQT_ENUM(QtCore, Qt::ApplicationAttribute);
PYQT_ENUM(QtCore, Qt::ApplicationState);
PYQT_ENUM(QtCore, Qt::ArrowType);
PYQT_ENUM(QtCore, Qt::AspectRatioMode);
PYQT_ENUM(QtCore, Qt::Axis);
PYQT_ENUM(QtCore, Qt::BGMode);
PYQT_ENUM(QtCore, Qt::BrushStyle);
PYQT_ENUM(QtCore, Qt::CaseSensitivity);
PYQT_ENUM(QtCore, Qt::CheckState);
PYQT_ENUM(QtCore, Qt::ChecksumType);
PYQT_ENUM(QtCore, Qt::ClipOperation);
PYQT_ENUM(QtCore, Qt::ConnectionType);
PYQT_ENUM(QtCore, Qt::ContextMenuPolicy);
PYQT_ENUM(QtCore, Qt::CoordinateSystem);
PYQT_ENUM(QtCore, Qt::Corner);
PYQT_ENUM(QtCore, Qt::CursorMoveStyle);
PYQT_ENUM(QtCore, Qt::CursorShape);
PYQT_ENUM(QtCore, Qt::DateFormat);
PYQT_ENUM(QtCore, Qt::DayOfWeek);
PYQT_ENUM(QtCore, Qt::DockWidgetArea);
PYQT_ENUM(QtCore, Qt::DropAction);
PYQT_ENUM(QtCore, Qt::Edge);
PYQT_ENUM(QtCore, Qt::EnterKeyType);
PYQT_ENUM(QtCore, Qt::EventPriority);
PYQT_ENUM(QtCore, Qt::FillRule);
PYQT_ENUM(QtCore, Qt::FindChildOption);
PYQT_ENUM(QtCore, Qt::FocusPolicy);
PYQT_ENUM(QtCore, Qt::FocusReason);
PYQT_ENUM(QtCore, Qt::GestureFlag);
PYQT_ENUM(QtCore, Qt::GestureState);
PYQT_ENUM(QtCore, Qt::GestureType);
PYQT_ENUM(QtCore, Qt::GlobalColor);
PYQT_ENUM(QtCore, Qt::HitTestAccuracy);
PYQT_ENUM(QtCore, Qt::ImageConversionFlag);
PYQT_ENUM(QtCore, Qt::InputMethodHint);
PYQT_ENUM(QtCore, Qt::InputMethodQuery);
PYQT_ENUM(QtCore, Qt::ItemDataRole);
PYQT_ENUM(QtCore, Qt::ItemFlag);
PYQT_ENUM(QtCore, Qt::ItemSelectionMode);
PYQT_ENUM(QtCore, Qt::ItemSelectionOperation);
PYQT_ENUM(QtCore, Qt::Key);
PYQT_ENUM(QtCore, Qt::KeyboardModifier);
PYQT_ENUM(QtCore, Qt::LayoutDirection);
PYQT_ENUM(QtCore, Qt::MaskMode);
PYQT_ENUM(QtCore, Qt::MatchFlag);
PYQT_ENUM(QtCore, Qt::Modifier);
PYQT_ENUM(QtCore, Qt::MouseButton);
PYQT_ENUM(QtCore, Qt::MouseEventFlag);
PYQT_ENUM(QtCore, Qt::MouseEventSource);
PYQT_ENUM(QtCore, Qt::NativeGestureType);
PYQT_ENUM(QtCore, Qt::NavigationMode);
PYQT_ENUM(QtCore, Qt::Orientation);
PYQT_ENUM(QtCore, Qt::PenCapStyle);
PYQT_ENUM(QtCore, Qt::PenJoinStyle);
PYQT_ENUM(QtCore, Qt::PenStyle);
PYQT_ENUM(QtCore, Qt::ScreenOrientation);
PYQT_ENUM(QtCore, Qt::ScrollBarPolicy);
PYQT_ENUM(QtCore, Qt::ScrollPhase);
PYQT_ENUM(QtCore, Qt::ShortcutContext);
PYQT_ENUM(QtCore, Qt::SizeHint);
PYQT_ENUM(QtCore, Qt::SizeMode);
PYQT_ENUM(QtCore, Qt::SortOrder);
PYQT_ENUM(QtCore, Qt::TabFocusBehavior);
PYQT_ENUM(QtCore, Qt::TextElideMode);
PYQT_ENUM(QtCore, Qt::TextFlag);
PYQT_ENUM(QtCore, Qt::TextFormat);
PYQT_ENUM(QtCore, Qt::TextInteractionFlag);
PYQT_ENUM(QtCore, Qt::TileRule);
PYQT_ENUM(QtCore, Qt::TimeSpec);
PYQT_ENUM(QtCore, Qt::TimerType);
PYQT_ENUM(QtCore, Qt::ToolBarArea);
PYQT_ENUM(QtCore, Qt::ToolButtonStyle);
PYQT_ENUM(QtCore, Qt::TransformationMode);
PYQT_ENUM(QtCore, Qt::WhiteSpaceMode);
PYQT_ENUM(QtCore, Qt::WidgetAttribute);
PYQT_ENUM(QtCore, Qt::WindowFrameSection);
PYQT_ENUM(QtCore, Qt::WindowModality);
PYQT_ENUM(QtCore, Qt::WindowState);
PYQT_ENUM(QtCore, Qt::WindowType);

PYQT_ENUM(QtWidgets, QMessageBox::ButtonRole);
PYQT_ENUM(QtWidgets, QMessageBox::DialogCode);
PYQT_ENUM(QtWidgets, QMessageBox::Icon);
PYQT_ENUM(QtWidgets, QMessageBox::PaintDeviceMetric);
PYQT_ENUM(QtWidgets, QMessageBox::RenderFlag);
PYQT_ENUM(QtWidgets, QMessageBox::StandardButton);

#undef PYQT_ENUM

#endif

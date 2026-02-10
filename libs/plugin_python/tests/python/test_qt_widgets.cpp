#include "pybind11_qt/pybind11_qt.h"

#include <pybind11/pybind11.h>

#include <QMap>
#include <QWidget>

namespace py = pybind11;
using namespace pybind11::literals;

QMap<QString, QWidget*> s_Widgets;
QWidget* s_Parent;

class CustomWidget : public QWidget {
public:
    CustomWidget(QString const& name, QWidget* parent = nullptr) : QWidget(parent)
    {
        s_Widgets[name] = this;
        setProperty("name", name);
    }

    ~CustomWidget() { s_Widgets.remove(property("name").toString()); }
};

class PyCustomWidget : public CustomWidget {
public:
    using CustomWidget::CustomWidget;
    int heightForWidth(int value) const
    {
        PYBIND11_OVERRIDE(int, CustomWidget, heightForWidth, value);
    }
};

PYBIND11_MODULE(qt_widgets, m)
{
    s_Parent = new QWidget();

    py::class_<CustomWidget, PyCustomWidget, py::qt::qobject_holder<CustomWidget>>
        pyCustomWidget(m, "CustomWidget");
    pyCustomWidget
        .def(py::init<QString, QWidget*>(), "name"_a, "parent"_a = (QWidget*)nullptr)
        .def("set_parent_cpp", [](CustomWidget* w) {
            w->setParent(s_Parent);
        });
    py::qt::add_qt_delegate<QWidget>(pyCustomWidget, "_widget");

    m.def("is_alive", [](QString const& name) {
        return s_Widgets.contains(name);
    });

    m.def("get", [](QString const& name) {
        return s_Widgets.contains(name) ? s_Widgets[name] : nullptr;
    });

    m.def("set_parent", [](QWidget* widget) {
        widget->setParent(s_Parent);
    });

    m.def("is_owned_cpp", [](QString const& name) {
        return s_Widgets.contains(name) && s_Widgets[name]->parent() == s_Parent;
    });

    m.def("make_widget_own_cpp", [](QString const& name) -> QWidget* {
        return new CustomWidget(name, s_Parent);
    });

    m.def(
        "make_widget_own_py",
        [](QString const& name) -> QWidget* {
            return new CustomWidget(name);
        },
        py::return_value_policy::take_ownership);

    // simply passing the widget gives the ownership of the Python object to C++
    m.def("send_to_cpp", [](QString const& name, QWidget* widget) {
        widget->setProperty("name", name);
        widget->setParent(s_Parent);
        s_Widgets[name] = widget;
    });

    m.def("heightForWidth", [](QString const& name, int value) {
        return s_Widgets.contains(name) ? s_Widgets[name]->heightForWidth(value)
                                        : -1024;
    });
}

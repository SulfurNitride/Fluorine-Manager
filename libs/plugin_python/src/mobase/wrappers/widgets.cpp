#include "wrappers.h"

#include "../pybind11_all.h"

#include <uibase/report.h>

namespace py = pybind11;
using namespace MOBase;

namespace mo2::python {

    void add_widget_bindings(pybind11::module_ m)
    {
        // TaskDialog is also in Windows System.
        using TaskDialog = MOBase::TaskDialog;

        // TaskDialog
        py::class_<TaskDialogButton>(m, "TaskDialogButton")
            .def(py::init<QString, QString, QMessageBox::StandardButton>(),
                 py::arg("text"), py::arg("description"), py::arg("button"))
            .def(py::init<QString, QMessageBox::StandardButton>(), py::arg("text"),
                 py::arg("button"))
            .def_readwrite("text", &TaskDialogButton::text)
            .def_readwrite("description", &TaskDialogButton::description)
            .def_readwrite("button", &TaskDialogButton::button);

        py::class_<TaskDialog>(m, "TaskDialog")
            .def(py::init([](QWidget* parent, QString const& title, QString const& main,
                             QString const& content, QString const& details,
                             QMessageBox::Icon icon,
                             std::vector<TaskDialogButton> const& buttons,
                             std::variant<QString, std::tuple<QString, QString>> const&
                                 remember) {
                     auto* dialog = new TaskDialog(parent, title);
                     dialog->main(main).content(content).details(details).icon(icon);

                     for (auto& button : buttons) {
                         dialog->button(button);
                     }

                     std::visit(
                         [dialog](auto const& item) {
                             QString action, file;
                             if constexpr (std::is_same_v<std::decay_t<decltype(item)>,
                                                          QString>) {
                                 action = item;
                             }
                             else {
                                 action = std::get<0>(item);
                                 file   = std::get<1>(item);
                             }
                             dialog->remember(action, file);
                         },
                         remember);

                     return dialog;
                 }),
                 py::return_value_policy::take_ownership,
                 py::arg("parent") = static_cast<QWidget*>(nullptr),
                 py::arg("title") = "", py::arg("main") = "", py::arg("content") = "",
                 py::arg("details") = "", py::arg("icon") = QMessageBox::NoIcon,
                 py::arg("buttons")  = std::vector<TaskDialogButton>{},
                 py::arg("remember") = "")
            .def("setTitle", &TaskDialog::title, py::arg("title"))
            .def("setMain", &TaskDialog::main, py::arg("main"))
            .def("setContent", &TaskDialog::content, py::arg("content"))
            .def("setDetails", &TaskDialog::details, py::arg("details"))
            .def("setIcon", &TaskDialog::icon, py::arg("icon"))
            .def("addButton", &TaskDialog::button, py::arg("button"))
            .def("setRemember", &TaskDialog::remember, py::arg("action"),
                 py::arg("file") = "")
            .def("setWidth", &TaskDialog::setWidth, py::arg("width"))
            .def("addContent", &TaskDialog::addContent, py::arg("widget"))
            .def("exec", &TaskDialog::exec);
    }

}  // namespace mo2::python

#pragma warning(disable : 4100)
#pragma warning(disable : 4996)

#include <format>
#include <tuple>
#include <variant>

#include <pybind11/embed.h>

#include "pybind11_all.h"

#include "wrappers/pyfiletree.h"
#include "wrappers/wrappers.h"

using namespace MOBase;
namespace py = pybind11;

PYBIND11_MODULE(mobase, m)
{
    using namespace mo2::python;

    m.add_object("PyQt6", py::module_::import("PyQt6"));
    m.add_object("PyQt6.QtCore", py::module_::import("PyQt6.QtCore"));
    m.add_object("PyQt6.QtGui", py::module_::import("PyQt6.QtGui"));
    m.add_object("PyQt6.QtWidgets", py::module_::import("PyQt6.QtWidgets"));

    // bindings
    //
    mo2::python::add_basic_bindings(m);
    mo2::python::add_wrapper_bindings(m);

    // game features must be added before plugins
    mo2::python::add_game_feature_bindings(m);
    mo2::python::add_igamefeatures_classes(m);

    mo2::python::add_plugins_bindings(m);

    // widgets
    //
    py::module_ widgets = m.def_submodule("widgets");
    mo2::python::add_widget_bindings(widgets);

    // utils
    //
    py::module_ utils = m.def_submodule("utils");
    mo2::python::add_utils_bindings(utils);

    // functions
    //
    m.def("getFileVersion", wrap_for_filepath(&MOBase::getFileVersion),
          py::arg("filepath"));
    m.def("getProductVersion", wrap_for_filepath(&MOBase::getProductVersion),
          py::arg("executable"));
    m.def("getIconForExecutable", wrap_for_filepath(&MOBase::iconForExecutable),
          py::arg("executable"));

    // typing stuff to be consistent with stubs and allow plugin developers to properly
    // type their code if they want
    {
        m.add_object("TypeVar", py::module_::import("typing").attr("TypeVar"));

        auto s = m.attr("__dict__");

        // expose MoVariant
        //
        // this needs to be defined, otherwise MoVariant is not found when actually
        // running plugins through MO2, making them crash (if plugins use MoVariant in
        // their own code)
        //
        m.add_object(
            "MoVariant",
            py::eval("None | bool | int | str | list[object] | dict[str, object]"));

        // same thing for GameFeatureType
        //
        m.add_object("GameFeatureType", py::eval("TypeVar('GameFeatureType')", s));
    }

    // private stuff for debugging/test
    py::module_ moprivate = m.def_submodule("private");

    // expose a function to create a particular tree, only for debugging
    // purpose, not in mobase.
    mo2::python::add_make_tree_function(moprivate);
    moprivate.def("extract_plugins", &mo2::python::extract_plugins);
}

#ifndef PYTHON_PYBIND11_QT_DETAILS_ENUM_HPP
#define PYTHON_PYBIND11_QT_DETAILS_ENUM_HPP

#include <type_traits>

#include <pybind11/pybind11.h>

#include "pybind11_qt_utils.h"

namespace pybind11::detail::qt {

    // EnumData, with static members (const char[])
    // - package: name of the Python package containing the enum (e.g.,
    // PyQt6.QtCore)
    // - name: full path to the enum, e.g. Qt.QGlobalColor
    //
    template <typename T>
    struct EnumData;

    // template class for most Qt types that have Python equivalent (QWidget,
    // etc.)
    //
    template <class Enum>
    struct qt_enum_caster {

    public:
        PYBIND11_TYPE_CASTER(Enum, EnumData<Enum>::package + const_name(".") +
                                       EnumData<Enum>::name);

        bool load(pybind11::handle src, bool)
        {
            if (PyLong_Check(src.ptr())) {
                value = static_cast<Enum>(PyLong_AsLong(src.ptr()));
                return true;
            }

            auto pyenum =
                get_attr_rec(EnumData<Enum>::package.text, EnumData<Enum>::name.text);

            if (isinstance(src, pyenum)) {
                value = static_cast<Enum>(src.attr("value").cast<int>());
                return true;
            }

            return false;
        }

        static pybind11::handle cast(Enum src,
                                     pybind11::return_value_policy /* policy */,
                                     pybind11::handle /* parent */)
        {
            auto pyenum =
                get_attr_rec(EnumData<Enum>::package.text, EnumData<Enum>::name.text);
            return pyenum(static_cast<int>(src));
        }
    };
}  // namespace pybind11::detail::qt

#endif

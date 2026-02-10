#include "pybind11_qt/details/pybind11_qt_utils.h"

namespace pybind11::detail::qt {

    pybind11::object get_attr_rec(std::string_view package, std::string_view path)
    {

        return module_::import("operator")
            .attr("attrgetter")(path.data())(module_::import(package.data()));
    }
}  // namespace pybind11::detail::qt

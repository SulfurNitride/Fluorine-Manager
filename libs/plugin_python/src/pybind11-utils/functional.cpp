#include "pybind11_utils/functional.h"

namespace py = pybind11;

namespace mo2::python::detail {

    bool has_compatible_arity(py::function fn, std::size_t arity)
    {
        auto inspect    = py::module_::import("inspect");
        auto arg_spec   = inspect.attr("getfullargspec")(fn);
        py::object args = arg_spec.attr("args"), varargs = arg_spec.attr("varargs"),
                   defaults = arg_spec.attr("defaults");

        auto args_count     = args.is(py::none()) ? 0 : py::len(args);
        auto defaults_count = defaults.is(py::none()) ? 0 : py::len(defaults);

        if (inspect.attr("ismethod")(fn).cast<bool>() && py::hasattr(fn, "__self__")) {
            --args_count;
        }

        auto required_count = args_count - defaults_count;

        return required_count <= arity  // cannot require more parameters than given,
               && (args_count >= arity ||
                   !varargs.is_none());  // must accept enough parameters.
    }

}  // namespace mo2::python::detail

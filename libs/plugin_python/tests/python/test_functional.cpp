#include "pybind11_utils/functional.h"

#include <pybind11/pybind11.h>

PYBIND11_MODULE(functional, m)
{
    m.def("fn_0_arg", [](std::function<int()> const& fn) {
        return fn();
    });

    m.def("fn_1_arg", [](std::function<int(int)> const& fn, int a) {
        return fn(a);
    });

    m.def("fn_2_arg", [](std::function<int(int, int)> const& fn, int a, int b) {
        return fn(a, b);
    });

    m.def("fn_0_or_1_arg", [](std::function<int()> const& fn) {
        return fn();
    });

    m.def("fn_0_or_1_arg", [](std::function<int(int)> const& fn) {
        return fn(1);
    });

    m.def("fn_1_or_2_or_3_arg", [](std::function<int(int)> const& fn) {
        return fn(1);
    });

    m.def("fn_1_or_2_or_3_arg", [](std::function<int(int, int)> const& fn) {
        return fn(1, 2);
    });

    m.def("fn_1_or_2_or_3_arg", [](std::function<int(int, int, int)> const& fn) {
        return fn(1, 2, 3);
    });
}

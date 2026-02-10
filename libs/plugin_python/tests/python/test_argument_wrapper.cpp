#include "pybind11_utils/smart_variant_wrapper.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>

namespace mo2::python::detail {

    template <>
    struct smart_variant_converter<std::string> {
        static std::string from(int const& value) { return std::to_string(value); }
    };

    template <>
    struct smart_variant_converter<int> {
        static int from(std::string const& value) { return std::stoi(value); }
    };

}  // namespace mo2::python::detail

// wrapper that can be constructed from
using Wrapper = mo2::python::smart_variant<int, std::string>;

template <std::size_t... Is, class Fn>
auto wrap(Fn&& fn)
{
    return mo2::python::wrap_arguments<Wrapper, Is...>(std::forward<Fn>(fn));
}

std::string fn1(std::string const& value)
{
    return value + "-" + value;
}

int fn2(int value)
{
    return value * 2;
}

std::string fn3(int value, std::vector<int> values, std::string const& name)
{
    return name + "-" + std::to_string(value + values.size());
}

PYBIND11_MODULE(argument_wrapper, m)
{
    m.def("fn1_raw", &fn1);
    m.def("fn1_wrap", wrap(&fn1));
    m.def("fn1_wrap_0", wrap<0>(&fn1));

    m.def("fn2_raw", &fn2);
    m.def("fn2_wrap", wrap(&fn2));
    m.def("fn2_wrap_0", wrap<0>(&fn2));

    m.def("fn3_raw", &fn3);
    m.def("fn3_wrap", wrap(&fn3));
    m.def("fn3_wrap_0", wrap<0>(&fn3));
    m.def("fn3_wrap_2", wrap<2>(&fn3));
    m.def("fn3_wrap_0_2", wrap<0, 2>(&fn3));
}

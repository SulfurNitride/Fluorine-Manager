#ifndef PYTHON_PYBIND11_GENERATOR_H
#define PYTHON_PYBIND11_GENERATOR_H

#include <generator>

#include <pybind11/pybind11.h>

namespace mo2::python {

    // the code here is mostly taken from pybind11 itself, and relies on some pybind11
    // internals so might be subject to change when upgrading pybind11 versions

    namespace detail {
        template <typename T>
        struct generator_state {
            std::generator<T> g;
            decltype(g.begin()) it;

            generator_state(std::generator<T> gen) : g(std::move(gen)), it(g.begin()) {}
        };
    }  // namespace detail

    // create a Python generator from a C++ generator
    //
    template <typename T, typename... Args>
    auto make_generator(std::generator<T> g, Args&&... args)
    {
        using state = detail::generator_state<T>;

        namespace py = pybind11;
        if (!py::detail::get_type_info(typeid(state), false)) {
            py::class_<state>(py::handle(), "iterator", pybind11::module_local())
                .def("__iter__",
                     [](state& s) -> state& {
                         return s;
                     })
                .def(
                    "__next__",
                    [](state& s) -> T {
                        if (s.it != s.g.end()) {
                            T v = *s.it;
                            s.it++;
                            return v;
                        }
                        else {
                            throw py::stop_iteration();
                        }
                    },
                    std::forward<Args>(args)...);
        }

        return py::cast(state{std::move(g)});
    }

}  // namespace mo2::python

#endif

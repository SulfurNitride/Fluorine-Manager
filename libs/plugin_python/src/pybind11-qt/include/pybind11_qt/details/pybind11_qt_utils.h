#ifndef PYTHON_PYBIND11_QT_DETAILS_UTILS_HPP
#define PYTHON_PYBIND11_QT_DETAILS_UTILS_HPP

#include <string>
#include <string_view>

#include <pybind11/pybind11.h>

namespace pybind11::detail::qt {

    /**
     * @brief Convert a XXX::YYY compile time string to a XXX.YYY compile time
     * string. Only one :: is allowed.
     *
     */
    template <size_t N>
    constexpr descr<N - 2> qt_name_cpp2py(const char (&name)[N])
    {
        descr<N - 2> res;
        for (std::size_t i = 0, j = 0; i < N - 2; ++i) {

            res.text[i] = name[j];

            if (res.text[i] == ':') {
                res.text[i] = '.';
                j += 2;
            }
            else {
                ++j;
            }
        }
        return res;
    }

    /**
     * @brief Retrieve the class from the given package at the given path
     *
     * @param package Name of the module.
     * @param path Path to the class/object in the module.
     *
     * @return the object at the given path in the given module
     */
    pybind11::object get_attr_rec(std::string_view package, std::string_view path);

}  // namespace pybind11::detail::qt

#endif

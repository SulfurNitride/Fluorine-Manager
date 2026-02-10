#ifndef PYTHONRUNNER_UTILS_H
#define PYTHONRUNNER_UTILS_H

#include <string_view>

#include <pybind11/pybind11.h>

namespace mo2::python {

    /**
     * @brief Show a deprecation warning.
     *
     * This methods will print a warning in MO2 log containing the location of
     * the call to the deprecated function. If show_once is true, the
     * deprecation warning will only be logged the first time the function is
     * called at this location.
     *
     * @param name Name of the deprecated function.
     * @param message Deprecation message.
     * @param show_once Only show the message once per call location.
     */
    void show_deprecation_warning(std::string_view name, std::string_view message = "",
                                  bool show_once = true);

}  // namespace mo2::python

#endif

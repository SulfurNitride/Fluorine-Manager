#ifndef PYTHONRUNNER_UTILS_H
#define PYTHONRUNNER_UTILS_H

#include <string_view>

#include <pybind11/pybind11.h>

namespace mo2::python {

    /**
     * @brief Configure Python stdout and stderr to log to MO2.
     *
     */
    void configure_python_stream();

    /**
     * @brief Configure logging for MO2 python plugin.
     *
     * @param mobase The mobase module.
     */
    void configure_python_logging(pybind11::module_ mobase);

}  // namespace mo2::python

#endif

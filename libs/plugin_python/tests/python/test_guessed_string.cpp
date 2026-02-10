#include "pybind11_qt/pybind11_qt.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include <uibase/guessedvalue.h>

using namespace MOBase;

PYBIND11_MODULE(guessed_string, m)
{
    m.def("get_value", [](GuessedValue<QString> const& value) {
        return value.operator const QString&();
    });
    m.def("get_variants", [](GuessedValue<QString> const& value) {
        return value.variants();
    });

    m.def("set_from_callback",
          [](GuessedValue<QString>& value,
             std::function<void(GuessedValue<QString>&)> const& fn) {
              fn(value);
          });

    // note: the function needs to take the guessed string by pointer if constructed
    // from C++, this is to be taken into account when calling Python function (cf.
    // installers)
    m.def("get_from_callback",
          [](std::function<void(GuessedValue<QString>*)> const& fn) {
              GuessedValue<QString> value;
              fn(&value);
              return (QString)value;
          });
}

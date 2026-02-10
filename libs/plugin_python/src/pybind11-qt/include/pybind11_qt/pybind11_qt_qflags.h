#ifndef PYTHON_PYBIND11_QT_QFLAGS_HPP
#define PYTHON_PYBIND11_QT_QFLAGS_HPP

#include <QFlags>

#include <pybind11/pybind11.h>

namespace pybind11::detail {

    // QFlags
    //
    template <class T>
    struct type_caster<QFlags<T>> {
        PYBIND11_TYPE_CASTER(QFlags<T>, const_name("QFlags[") + make_caster<T>::name +
                                            const_name("]"));

        /**
         * Conversion part 1 (Python->C++): convert a PyObject into a QString
         * instance or return false upon failure. The second argument
         * indicates whether implicit conversions should be applied.
         */
        bool load(handle src, bool)
        {
            PyObject* tmp = PyNumber_Long(src.ptr());

            if (!tmp) {
                return false;
            }

            // we do an intermediate extraction to T but this actually
            // can contains multiple values
            T flag_value = static_cast<T>(PyLong_AsLong(tmp));
            Py_DECREF(tmp);

            value = QFlags<T>(flag_value);

            return !PyErr_Occurred();
        }

        /**
         * Conversion part 2 (C++ -> Python): convert an QString instance into
         * a Python object. The second and third arguments are used to
         * indicate the return value policy and parent object (for
         * ``return_value_policy::reference_internal``) and are generally
         * ignored by implicit casters.
         */
        static handle cast(QFlags<T> const& src, return_value_policy /* policy */,
                           handle /* parent */)
        {
            return PyLong_FromLong(static_cast<int>(src));
        }
    };

}  // namespace pybind11::detail

#endif

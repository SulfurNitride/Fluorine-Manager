#ifndef PYTHON_PYBIND11_QT_BASIC_HPP
#define PYTHON_PYBIND11_QT_BASIC_HPP

#include <QString>
#include <QVariant>

#include <pybind11/pybind11.h>

namespace pybind11::detail {

    // QString
    //
    template <>
    struct type_caster<QString> {
        PYBIND11_TYPE_CASTER(QString, const_name("str"));

        bool load(handle src, bool);

        static handle cast(QString src, return_value_policy policy, handle parent);
    };

    // QVariant - this needs to be defined BEFORE QVariantList
    //
    template <>
    struct type_caster<QVariant> {
    public:
        PYBIND11_TYPE_CASTER(QVariant, const_name("MoVariant"));

        bool load(handle src, bool);

        static handle cast(QVariant var, return_value_policy policy, handle parent);
    };

}  // namespace pybind11::detail

#endif

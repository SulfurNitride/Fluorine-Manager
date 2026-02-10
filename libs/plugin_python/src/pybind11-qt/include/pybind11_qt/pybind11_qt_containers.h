#ifndef PYTHON_PYBIND11_QT_CONTAINERS_HPP
#define PYTHON_PYBIND11_QT_CONTAINERS_HPP

#include <QList>
#include <QMap>
#include <QSet>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// this needs to be included here to get proper QVariantList and QVariantMap
#include "details/pybind11_qt_qlist.h"
#include "details/pybind11_qt_qmap.h"
#include "pybind11_qt_basic.h"

namespace pybind11::detail {

    // QList
    //
    template <class T>
    struct type_caster<QList<T>> : qt::qlist_caster<QList<T>, T> {};

    // QSet
    //
    template <class T>
    struct type_caster<QSet<T>> : set_caster<QList<T>, T> {};

    // QMap
    //
    template <class K, class V>
    struct type_caster<QMap<K, V>> : qt::qmap_caster<QMap<K, V>, K, V> {};

    // QStringList
    //
    template <>
    struct type_caster<QStringList> : qt::qlist_caster<QStringList, QString> {};

    // QVariantList
    //
    template <>
    struct type_caster<QVariantList> : qt::qlist_caster<QVariantList, QVariant> {};

    // QVariantMap
    //
    template <>
    struct type_caster<QVariantMap> : qt::qmap_caster<QVariantMap, QString, QVariant> {
    };

}  // namespace pybind11::detail

#endif

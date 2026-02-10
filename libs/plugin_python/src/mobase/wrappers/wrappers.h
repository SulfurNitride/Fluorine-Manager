#ifndef PYTHON_WRAPPERS_WRAPPERS_H
#define PYTHON_WRAPPERS_WRAPPERS_H

#include <any>
#include <map>
#include <typeindex>

#include <pybind11/pybind11.h>

#include <QList>
#include <QObject>

#include <uibase/iplugingame.h>

namespace mo2::python {

    /**
     * @brief Add bindings for the various classes in uibase that are not
     * wrappers (i.e., cannot be extended from Python).
     *
     * @param m Python module to add bindings to.
     */
    void add_basic_bindings(pybind11::module_ m);

    /**
     * @brief Add bindings for the various custom widget classes in uibase that
     * cannot be extended from Python.
     *
     * @param m Python module to add bindings to.
     */
    void add_widget_bindings(pybind11::module_ m);

    /**
     * @brief Add bindings for the various utilities classes and functions in uibase
     * that cannot be extended from Python.
     *
     * @param m Python module to add bindings to.
     */
    void add_utils_bindings(pybind11::module_ m);

    /**
     * @brief Add bindings for the uibase wrappers to the given module. uibase
     * wrappers include classes from uibase that can be extended from Python but
     * are neither plugins nor game features (e.g., ISaveGame).
     *
     * @param m Python module to add bindings to.
     */
    void add_wrapper_bindings(pybind11::module_ m);

    /**
     * @brief Add bindings for the various plugin classes in uibase that can be
     * extended from Python.
     *
     * @param m Python module to add bindings to.
     */
    void add_plugins_bindings(pybind11::module_ m);

    /**
     * @brief Extract plugins from the given object. For each plugin implemented, an
     * object is returned.
     *
     * The returned QObject* are set as owner of the given object so that the Python
     * object lifetime does not end immediately after returning to C++.
     *
     * @param object Python object to extract plugins from.
     *
     * @return a QObject* for each plugin implemented by the given object.
     */
    QList<QObject*> extract_plugins(pybind11::object object);

    /**
     * @brief Add bindings for the various game features classes in uibase that
     * can be extended from Python.
     *
     * @param m Python module to add bindings to.
     */
    void add_game_feature_bindings(pybind11::module_ m);

    /**
     * @brief Add bindings for IGameFeatures.
     *
     * @param m Python module to add bindings to.
     */
    void add_igamefeatures_classes(pybind11::module_ m);

    /**
     * @brief Extract the game feature corresponding to the given Python type.
     *
     * @param gameFeatures Game features to extract the feature from.
     * @param type Type of the feature to extract.
     *
     * @return the feature from the game, or None is the game as no such feature.
     */
    pybind11::object extract_feature(MOBase::IGameFeatures const& gameFeatures,
                                     pybind11::object type);

    /**
     * @brief Unregister the game feature corresponding to the given Python type.
     *
     * @param gameFeatures Game features to unregister the feature from.
     * @param type Type of the feature to unregister.
     *
     * @return the feature from the game, or None is the game as no such feature.
     */
    int unregister_feature(MOBase::IGameFeatures& gameFeatures, pybind11::object type);

}  // namespace mo2::python

#endif  // PYTHON_WRAPPERS_WRAPPERS_H

#ifndef PYTHON_PYBIND11_QT_HPP
#define PYTHON_PYBIND11_QT_HPP

// this header defines many type casters for Qt types, including:
// - basic Qt types such as QString and QVariant - those do not have PyQt6 equivalent
// - QFlags<> class templates
// - containers such as QList<>, QSet<>, etc., the QList<> casters is more flexible than
//   the std::vector<> or std::list<> ones as it accepts any iterable
// - many Qt enumeration types (see pybind11_qt_enums)
// - many Qt classes with PyQt6 equivalent
//   - copyable type are copied between Python and C++
//   - non-copyable type (QObject, QWidget, QMainWindow) are always owned by the C++
//     side, even when constructed on the Python side, and owned their corresponding
//     Python object, e.g., an instance of a class inheriting QWidget created on the
//     Python side can be safely used in C++ since the Python object will be owned by
//     the C++ QWidget object
//

#include "pybind11_qt_basic.h"
#include "pybind11_qt_containers.h"
#include "pybind11_qt_enums.h"
#include "pybind11_qt_holder.h"
#include "pybind11_qt_objects.h"
#include "pybind11_qt_qflags.h"

namespace pybind11::qt {

    /**
     * @brief Tie the lifetime of the Python object to the lifetime of the given
     * QObject.
     *
     * @param owner QObject that will own the python object.
     * @param child Python object that the QObject will own.
     */
    inline void set_qt_owner(QObject* owner, object child)
    {
        new detail::qt::qobject_holder_impl{owner, child};
    }

    /**
     * @brief Tie the lifetime of the given object to the lifetime of the corresponding
     * Python object.
     *
     * This object must have been created from Python and must inherit QObject.
     *
     * @param object Object to tie.
     */
    template <typename Class>
    void set_qt_owner(Class* object)
    {
        static_assert(std::is_base_of_v<QObject, Class>);
        new detail::qt::qobject_holder_impl{object};
    }

    /**
     * @brief Add Qt "delegate" to the given class.
     *
     * This function defines two methods: __getattr__ and name, where name will
     * simply return the PyQtX object as a QClass* object, while __getattr__
     * will delegate to the underlying QClass object when required.
     *
     * This allow access to Qt interface for object exposed using pybind11
     * (e.g., signals, methods from QObject or QWidget, etc.).
     *
     * @param pyclass Python class to define the methods on.
     * @param name Name of the method to retrieve the underlying object.
     *
     * @tparam QClass Name of the Qt class, cannot be deduced.
     * @tparam Class Class being wrapped, deduced.
     * @tparam Args... Arguments of the class template parameters, deduced.
     */
    template <class QClass, class Class, class... Args>
    auto& add_qt_delegate(pybind11::class_<Class, Args...>& pyclass, const char* name)
    {
        return pyclass
            .def(name,
                 [](Class* w) -> QClass* {
                     return w;
                 })
            .def(
                "__getattr__", +[](Class* w, pybind11::str str) -> pybind11::object {
                    return pybind11::cast((QClass*)w).attr(str);
                });
    }

}  // namespace pybind11::qt

#endif

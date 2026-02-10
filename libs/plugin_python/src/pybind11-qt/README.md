# pybind11-qt

This library contains code to interface pybind11 with Qt and PyQt.

## Type casters

The main part of this library is a set of (templated) type casters for Qt types that
can be used by simply importing `pybind11_qt/pybind11_qt.h`.
This provides type casters for:

- Standard Qt types, such as `QString` or `QVariant`.
  - `QString` is equivalent to Python `str` (unicode or not).
  - `QVariant` is not exposed but the object is directly converted, similarly to
    `std::variant` default type caster.
- Qt containers (`QList`, `QSet`, `QMap`, `QStringList`).
  - The `QList` type-caster is more flexible than the standard container type-casters
    from pybind11 as it accepts any iterable.
- `QFlags` - Delegates the cast to the underlying type, basically.
- Qt enumerations: a lot of enumerations are provided in
  [`pybind11_qt_enums.h`](include/pybind11_qt/pybind11_qt_enums.h) and new ones can be
  easily added using the `PYQT_ENUM` macro (inside the header file).
- Qt objects: very few are provided in
  [`pybind11_qt_objects.h`](include/pybind11_qt/pybind11_qt_objects.h) and new ones can
  be added using the `PYQT_OBJECT` macro (inside the header file).
  - Copy-constructible Qt objects are copied when passing from C++ to Python or
    vice-versa.
  - Non copy-constructible Qt objects, e.g., `QObject` or `QWidget` should always be
    exposed as pointer, and their ownership is transferred to C++ when coming from
    Python.

## Qt holder

The library also provides a `pybind11::qt::qobject_holder` holder for pybind11 that
transfer ownerships of the Python object to the underlying `QObject`.

This holder is useful when exposing classes inheriting `QObject` (or a child class of
`QObject`) that can be extended to Python.

The library also provides two `set_qt_owner` functions that can be used to transfer
ownership manually.

## Qt delegates

The library provides a `add_qt_delegate` function that can be used to delegate Python
call to Qt functions to C++:

```cpp
py::class_<
    // the C++ class extending QObject to expose
    ISaveGameInfoWidget,

    // the trampoline class
    PySaveGameInfoWidget,

    // the Qt holder to keep the Python object alive alongside the C++ one
    py::qt::qobject_holder<ISaveGameInfoWidget>

> iSaveGameInfoWidget(m, "ISaveGameInfoWidget");

// allow to access most of the class attributes through Python via an overload of
// __getattr__ and add a _widget() method to access the widget itself if needed
//
py::qt::add_qt_delegate<QWidget>(iSaveGameInfoWidget, "_widget");
```

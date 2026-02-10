# pybind11-utils

This library contains some utility stuff for `pybind11`

## smart_variant_wrapper.h

Expose a function `mo2::python::wrap_arguments` and a template
`mo2::python::smart_variant` that can be used to expose more interesting types Python
than the C++ one, e.g., accept `os.PathLike` and `QFileInfo` when a simple `QString` is
expected.

A toy example can be found in the test folder at
[`tests/python/test_argument_wrapper.cpp`](../../tests/python/test_argument_wrapper.cpp).

More concrete examples can be found in
[`mobase/pybind11_all.h`](../mobase/pybind11_all.h) for `FileWrapper` and
`DirectoryWrapper`.

## functional.h

TODO: updated version of `<pybind11/functional.h>` that should check the signature
a bit more when creating `std::function` (similar to previous implementation).

## shared_cpp_owner.h

Expose a macro `MO2_PYBIND11_SHARED_CPP_HOLDER` that can be used to declare that
`std::shared_ptr<...>` must hold-on their associate Python object.

```cpp
// use the macro on the type to be exposed (with a trampoline class)
MO2_PYBIND11_SHARED_CPP_HOLDER(ISaveGame)

// use std::shared_ptr<> as the holder for the class
py::class_<ISaveGame, PySaveGame, std::shared_ptr<ISaveGame>>(...);
```

Using the `MO2_PYBIND11_SHARED_CPP_HOLDER` (must be present in all files manipulating
`ISaveGame` between C++ and Python) ensure that the Python instance remains alive
alongside the C++ one.

The `MO2_PYBIND11_SHARED_CPP_HOLDER` declares a specialization of `type_caster<>` that
alters the `std::shared_ptr` by return a `std::shared_ptr<>` that owns the Python
object (via a `pybind11::object`) but does not release the C++ one - The C++ object is
owned by the Python one, so the relation is as follows:

- The `std::shared_ptr<X>` manipulated in C++ maintains the `pybind11::object` alive
  through a custom deleter but DOES NOT release the C++ object when the reference count
  reaches 0.
- The Python object holds a standard `std::shared_ptr<X>` that will release the object
  when the reference count reaches 0.

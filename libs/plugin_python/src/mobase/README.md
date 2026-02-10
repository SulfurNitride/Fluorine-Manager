# mobase

`mobase` is a ModOrganizer2 Python API.
It provides access to the part of
[`uibase`](https://github.com/ModOrganizer2/modorganizer-uibase) C++ API from
Python through [`pybind11`](https://github.com/pybind/pybind11).

## Organization

**Important:** All (most) files should include `pybind11_all.h` (either directly
or through another header) to get proper `type_caster` available.

- `mobase.cpp` contains the `PYBIND11_MODULE` definition of `mobase` but is otherwise
  the entrypoint for other functions.
- `wrappers.h` contains the declaration of most functions implemented under
  `wrappers/`.
- The other files under `wrappers/` contains bindings and trampoline classes (see
  below) for `uibase` classes.
  - `basic_classes.cpp` contains the bindings for most classes that cannot be extended
    in Python (`IOrganizer`, `IModInterface`, etc.)
  - `game_features.cpp` contains the bindings and trampoline classes for game features.
  - `pyfiletree.h` and `pyfiletree.cpp` contains bindings for the `IFileTree`-related
    classes.
  - `pyplugins.h` contains the trampoline classes for the `IPluginXXX` classes and
    `pyplugins.cpp` the bindings.
    - `pyplugins.h` is required since the trampoline classes are tagged with `Q_OBJECT`,
      and MOC does not work if the classes are declared in a C++ file.
    - `pyplugins.cpp` also contains the `extract_plugins` function in `mobase.private`
      that is used to extract plugins from Python object in the runner.
  - `widgets.cpp` contains the bindings for the widget classes.
  - `wrappers.cpp` contains the trampoline and bindings for non-plugin classes that can
    be extended through Python.

## Updating mobase

### Classes that cannot be extended through Python

Updating or adding classes that cannot be extended through Python is quite easily.
One simply needs to declare the appropriate `py::class_` or add new `.def()`.

See below for things to remember when creating pybind11 bindings.

### Free functions

Similar to classes that cannot be extended through Python, see above.

### Classes that can be extended through Python: Plugins

To extend plugins, simply update the trampoline classes in `pyplugins.h` and the
bindings in `pyplugins.cpp`.

**Note:** For new plugins, simply look at the existing one.

### Classes that can be extended through Python: Game Features

To extend or expose game features:

- Create (if there is not already one) a trampoline class for the feature in
  `game_features.cpp`.
  - Add implementation of missing functions if required.
- Add the bindings in `add_game_feature_bindings` in `game_features.cpp`.
- For new feature, add the feature type to `GameFeaturesHelper::GameFeatures` in
  `game_features.cpp`.

### Classes that can be extended through Python: Others

Non-plugin classes should be added to the `wrappers.cpp` file and should be exposed
with `std::shared_ptr<>` or `qobject_holder<>` holders.

- If the classes extends `QObject`, use a `qobject_holder`.
- Otherwise use a `std::shared_ptr<>` holder and add a `MO2_PYBIND11_SHARED_CPP_HOLDER`
  declaration in `pybind11_all.h`.

Trampoline can be defined directly in `wrappers.cpp`, and bindings in the appropriate
function.
See the existing classes for example.

**Important:**
You need to make sure that `uibase` manipulates such classes through
`std::shared_ptr<>` (unless those inherit `QObject`).
Using `std::unique_ptr<>` is not possible since `std::unique_ptr<>` cannot have custom
runtime-specified deleters.

## Things to remember

Here are a few things to remember when creating bindings:

- If a function has multiple overloads that can conflict in Python, the more complex
  one must be defined first as pybind11 will try calling them in order.
- If a C++ function expect a `QString`, `QFileInfo` or `QDir` that represents a file or
  a directory, wrapping the function with `wrap_for_filepath` or `wrap_for_directory` is
  a good idea. This allows Python to call the function with `pathlib.Path`.
- Most of the C++ function taking a reference to modify in C++ cannot be directly
  exposed in Python since Python cannot modify reference to simple type (e.g.
  `QString&` or `int&`).
  The best way to expose such function is to bind a lambda that returns a variant from
  Python, e.g.

```cpp
// assume the C++ function is QString fn(QString& foo, QString const& bar, int& maz);

m.def("function", [](QString& foo, QString const& bar, int& maz) {
  // call the function
  auto ret = function(foo, bar, maz);

  // make a tuple containing the return value (if there is one), and the modified
  // values passed by reference
  return std::make_tuple(ret, foo, maz);
});
```

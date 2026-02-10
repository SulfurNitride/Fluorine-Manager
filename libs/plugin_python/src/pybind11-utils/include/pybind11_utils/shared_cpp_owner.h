#ifndef PYTHON_PYBIND11_SHARED_CPP_OWNER_H
#define PYTHON_PYBIND11_SHARED_CPP_OWNER_H

#include <pybind11/pybind11.h>

// pybind11 has some issues when a Python classes extend a C++ wrapper since the Python
// object is not kept alive alongside the returned object
//
// there is a pybind11 branch called "smart_holder" that tries to solve this in a very
// complicated way (with many other features)
//
// here, we simply use a custom type_caster<> for the classes we need - see the actual
// definition in mo2::python::detail below
//
// IMPORTANT: this only works for classes that are managed by shared_ptr on the C++
// side, not Qt object (see pybind11-qt holder for that)
//

namespace mo2::python::detail {

    template <class Type, class SharedType>
    struct shared_cpp_owner_caster
        : pybind11::detail::copyable_holder_caster<Type, SharedType> {

        // note that the actual holder type might be different in term of constness
        using type        = Type;
        using holder_type = SharedType;

        using base = pybind11::detail::copyable_holder_caster<Type, SharedType>;
        using base::holder;
        using base::value;

        // in load, we use the default type_caster<> to extract the shared pointer, then
        // we replace it by a custom one
        //
        // the custom shared_ptr<> holds the py::object BUT does not really manage the
        // C++ object because it will ref-count but not delete it
        //
        // this should work because here it's how it works:
        // - the Python object holds a standard shared_ptr<> for the C++ object -> the
        // C++ object remains alive as long as the Python one remains alive
        // - the C++ object holds a shared_ptr<> that manages the python object -> the
        // Python object remains alive as-long as there is a shared_ptr<> on the C++
        // side
        //
        bool load(pybind11::handle src, bool convert)
        {
            namespace py = pybind11;

            if (!base::load(src, convert)) {
                return false;
            }

            holder.reset(holder.get(), [pyobj = py::reinterpret_borrow<py::object>(
                                            src)](auto*) mutable {
                py::gil_scoped_acquire s;
                pyobj = py::object();

                // we do NOT delete the object here - if this was the last reference to
                // the Python object, the Python object will delete it
            });

            return true;
        }

        // cast simply forward to the original type_caster<>
        //
        static pybind11::handle cast(const holder_type& src,
                                     pybind11::return_value_policy policy,
                                     pybind11::handle parent)
        {
            return base::cast(src, policy, parent);
        }
    };

}  // namespace mo2::python::detail

#define MO2_PYBIND11_SHARED_CPP_HOLDER(Type)                                           \
    namespace pybind11::detail {                                                       \
        template <>                                                                    \
        struct type_caster<std::shared_ptr<Type>>                                      \
            : mo2::python::detail::shared_cpp_owner_caster<Type,                       \
                                                           std::shared_ptr<Type>> {};  \
        template <>                                                                    \
        struct type_caster<std::shared_ptr<const Type>>                                \
            : mo2::python::detail::shared_cpp_owner_caster<                            \
                  Type, std::shared_ptr<const Type>> {};                               \
    }

#endif

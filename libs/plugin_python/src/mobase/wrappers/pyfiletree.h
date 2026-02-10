#ifndef MO2_PYTHON_FILETREE_H
#define MO2_PYTHON_FILETREE_H

#include "../pybind11_all.h"

#include <uibase/ifiletree.h>

namespace pybind11 {
    template <>
    struct polymorphic_type_hook<MOBase::FileTreeEntry> {
        static const void* get(const MOBase::FileTreeEntry* src,
                               const std::type_info*& type);
    };
}  // namespace pybind11

namespace mo2::python {

    /**
     * @brief Add bindings for FileTreeEntry andIFileTree to the given module.
     *
     * @param mobase Module to add the bindings to.
     */
    void add_ifiletree_bindings(pybind11::module_& m);

    /**
     * @brief Add makeTree() function to the given module, useful for debugging.
     *
     * @param mobase Module to add the function to.
     */
    void add_make_tree_function(pybind11::module_& m);

}  // namespace mo2::python

#endif

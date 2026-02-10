#include "pybind11_qt/pybind11_qt.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include <uibase/ifiletree.h>

using namespace MOBase;

PYBIND11_MODULE(filetree, m)
{
    // test the FileTypes stuff
    m.def("is_file", [](IFileTree::FileTypes const& t) {
        return t.testFlag(IFileTree::FILE);
    });
    m.def("is_directory", [](IFileTree::FileTypes const& t) {
        return t.testFlag(IFileTree::DIRECTORY);
    });
    m.def("value", [](IFileTree::FileTypes const& t) {
        return t.toInt();
    });
}

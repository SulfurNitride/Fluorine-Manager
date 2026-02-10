#include "wrappers.h"

#include "../pybind11_all.h"

#include <uibase/report.h>
#include <uibase/utility.h>

#ifdef _WIN32
#include "known_folders.h"
#endif

namespace py = pybind11;
using namespace MOBase;

namespace mo2::python {

    void add_utils_bindings(pybind11::module_ m)
    {
#ifdef _WIN32
        py::class_<KnownFolder> pyKnownFolder(m, "KnownFolder");
        for (std::size_t i = 0; i < KNOWN_FOLDERS.size(); ++i) {
            pyKnownFolder.attr(KNOWN_FOLDERS[i].name) = py::int_(i);
        }

        m.def(
            "getKnownFolder",
            [](std::size_t knownFolderId, QString what) {
                return getKnownFolder(KNOWN_FOLDERS.at(knownFolderId).guid, what);
            },
            py::arg("known_folder"), py::arg("what") = "");

        m.def(
            "getOptionalKnownFolder",
            [](std::size_t knownFolderId) {
                const auto r =
                    getOptionalKnownFolder(KNOWN_FOLDERS.at(knownFolderId).guid);
                return r.isEmpty() ? py::none{} : py::cast(r);
            },
            py::arg("known_folder"));
#endif

        m.def("getFileVersion", wrap_for_filepath(&MOBase::getFileVersion),
              py::arg("filepath"));
        m.def("getProductVersion", wrap_for_filepath(&MOBase::getProductVersion),
              py::arg("executable"));
        m.def("getIconForExecutable", wrap_for_filepath(&MOBase::iconForExecutable),
              py::arg("executable"));
    }

}  // namespace mo2::python

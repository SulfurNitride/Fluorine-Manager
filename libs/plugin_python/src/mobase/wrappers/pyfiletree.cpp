#include "pyfiletree.h"

#include <tuple>
#include <variant>

#include "../pybind11_all.h"

#include <uibase/ifiletree.h>
#include <uibase/log.h>

namespace py = pybind11;
using namespace MOBase;

namespace mo2::detail {

    // filetree implementation for testing purpose
    //
    class PyFileTree : public IFileTree {
    public:
        using callback_t = std::function<bool(QString, bool)>;

        PyFileTree(std::shared_ptr<const IFileTree> parent, QString name,
                   callback_t callback)
            : FileTreeEntry(parent, name), IFileTree(), m_Callback(callback)
        {
        }

        std::shared_ptr<FileTreeEntry> addFile(QString name, bool) override
        {
            if (m_Callback && !m_Callback(name, false)) {
                throw UnsupportedOperationException("File rejected by callback.");
            }
            return IFileTree::addFile(name);
        }

        std::shared_ptr<IFileTree> addDirectory(QString name) override
        {
            if (m_Callback && !m_Callback(name, true)) {
                throw UnsupportedOperationException("Directory rejected by callback.");
            }
            return IFileTree::addDirectory(name);
        }

    protected:
        std::shared_ptr<IFileTree>
        makeDirectory(std::shared_ptr<const IFileTree> parent,
                      QString name) const override
        {
            return std::make_shared<PyFileTree>(parent, name, m_Callback);
        }

        bool doPopulate([[maybe_unused]] std::shared_ptr<const IFileTree> parent,
                        std::vector<std::shared_ptr<FileTreeEntry>>&) const override
        {
            return true;
        }
        std::shared_ptr<IFileTree> doClone() const override
        {
            return std::make_shared<PyFileTree>(nullptr, name(), m_Callback);
        }

    private:
        callback_t m_Callback;
    };

}  // namespace mo2::detail

#pragma optimize("", off)

namespace pybind11 {
    const void* polymorphic_type_hook<FileTreeEntry>::get(const FileTreeEntry* src,
                                                          const std::type_info*& type)
    {
        if (auto p = dynamic_cast<const IFileTree*>(src)) {
            type = &typeid(IFileTree);
            return p;
        }
        return src;
    }
}  // namespace pybind11

namespace mo2::python {

    void add_ifiletree_bindings(pybind11::module_& m)
    {
        // FileTreeEntry class:
        auto fileTreeEntryClass =
            py::class_<FileTreeEntry, std::shared_ptr<FileTreeEntry>>(m,
                                                                      "FileTreeEntry");

        // IFileTree class:
        auto iFileTreeClass =
            py::class_<IFileTree, FileTreeEntry, std::shared_ptr<IFileTree>>(
                m, "IFileTree", py::multiple_inheritance());

        // this is FILE_OR_DIRECTORY but as a FileType since we kind of cheat for the
        // exposure in Python and this help pybind11 creates proper typing

        const auto FILE_OR_DIRECTORY = static_cast<FileTreeEntry::FileType>(
            FileTreeEntry::FILE_OR_DIRECTORY.toInt());

        // we do not use the enum directly, we will mostly bind the FileTypes
        // (with an S)
        py::enum_<FileTreeEntry::FileType>(fileTreeEntryClass, "FileTypes",
                                           py::arithmetic{})
            .value("FILE", FileTreeEntry::FileType::FILE)
            .value("DIRECTORY", FileTreeEntry::FileType::DIRECTORY)
            .value("FILE_OR_DIRECTORY", FILE_OR_DIRECTORY)
            .export_values();

        fileTreeEntryClass

            .def("isFile", &FileTreeEntry::isFile)
            .def("isDir", &FileTreeEntry::isDir)
            .def("fileType", &FileTreeEntry::fileType)
            .def("name", &FileTreeEntry::name)
            .def("suffix", &FileTreeEntry::suffix)
            .def(
                "hasSuffix",
                [](FileTreeEntry* entry, QStringList suffixes) {
                    return entry->hasSuffix(suffixes);
                },
                py::arg("suffixes"))
            .def(
                "hasSuffix",
                [](FileTreeEntry* entry, QString suffix) {
                    return entry->hasSuffix(suffix);
                },
                py::arg("suffix"))
            .def("parent", py::overload_cast<>(&FileTreeEntry::parent))
            .def("path", &FileTreeEntry::path, py::arg("sep") = "\\")
            .def("pathFrom", &FileTreeEntry::pathFrom, py::arg("tree"),
                 py::arg("sep") = "\\")

            // Mutable operation:
            .def("detach", &FileTreeEntry::detach)
            .def("moveTo", &FileTreeEntry::moveTo, py::arg("tree"))

            // Special methods:
            .def("__eq__",
                 [](const FileTreeEntry* entry, QString other) {
                     return entry->compare(other) == 0;
                 })
            .def("__eq__",
                 [](const FileTreeEntry* entry, std::shared_ptr<FileTreeEntry> other) {
                     return entry == other.get();
                 })

            // Special methods for debug:
            .def("__repr__", [](const FileTreeEntry* entry) {
                return "FileTreeEntry(\"" + entry->name() + "\")";
            });

        py::enum_<IFileTree::InsertPolicy>(iFileTreeClass, "InsertPolicy")
            .value("FAIL_IF_EXISTS", IFileTree::InsertPolicy::FAIL_IF_EXISTS)
            .value("REPLACE", IFileTree::InsertPolicy::REPLACE)
            .value("MERGE", IFileTree::InsertPolicy::MERGE)
            .export_values();

        py::enum_<IFileTree::WalkReturn>(iFileTreeClass, "WalkReturn")
            .value("CONTINUE", IFileTree::WalkReturn::CONTINUE)
            .value("STOP", IFileTree::WalkReturn::STOP)
            .value("SKIP", IFileTree::WalkReturn::SKIP)
            .export_values();

        // in C++ this is not an inner enum due to the conditional feature of glob(),
        // but in Python this makes more sense as a inner enum
        py::enum_<GlobPatternType>(iFileTreeClass, "GlobPatternType")
            .value("GLOB", GlobPatternType::GLOB)
            .value("REGEX", GlobPatternType::REGEX)
            .export_values();

        // Non-mutable operations:
        iFileTreeClass.def("exists",
                           py::overload_cast<QString, IFileTree::FileTypes>(
                               &IFileTree::exists, py::const_),
                           py::arg("path"), py::arg("type") = FILE_OR_DIRECTORY);
        iFileTreeClass.def(
            "find", py::overload_cast<QString, IFileTree::FileTypes>(&IFileTree::find),
            py::arg("path"), py::arg("type") = FILE_OR_DIRECTORY);
        iFileTreeClass.def("pathTo", &IFileTree::pathTo, py::arg("entry"),
                           py::arg("sep") = "\\");

        iFileTreeClass.def(
            "walk",
            py::overload_cast<
                std::function<IFileTree::WalkReturn(
                    QString const&, std::shared_ptr<const FileTreeEntry>)>,
                QString>(&IFileTree::walk, py::const_),
            py::arg("callback"), py::arg("sep") = "\\");

        // the walk() and glob() generator version are free functions in C++ due to the
        // conditional nature, but in Python, it makes more sense to have them as method
        // of IFileTree directly

        iFileTreeClass.def("walk", [](std::shared_ptr<const IFileTree> tree) {
            return make_generator(walk(tree));
        });

        iFileTreeClass.def(
            "glob",
            [](std::shared_ptr<const IFileTree> tree, QString pattern,
               GlobPatternType patternType) {
                return make_generator(glob(tree, pattern, patternType));
            },
            py::arg("pattern"), py::arg("type") = GlobPatternType::GLOB);

        // Kind-of-static operations:
        iFileTreeClass.def("createOrphanTree", &IFileTree::createOrphanTree,
                           py::arg("name") = "");

        // addFile() and addDirectory throws exception instead of returning null
        // pointer in order to have better traces.
        iFileTreeClass.def(
            "addFile",
            [](IFileTree* w, QString path, bool replaceIfExists) {
                auto result = w->addFile(path, replaceIfExists);
                if (result == nullptr) {
                    throw std::logic_error("addFile failed");
                }
                return result;
            },
            py::arg("path"), py::arg("replace_if_exists") = false);
        iFileTreeClass.def(
            "addDirectory",
            [](IFileTree* w, QString path) {
                auto result = w->addDirectory(path);
                if (result == nullptr) {
                    throw std::logic_error("addDirectory failed");
                }
                return result;
            },
            py::arg("path"));

        // Merge needs custom return types depending if the user wants overrides
        // or not. A failure is translated into an exception for easier tracing
        // and handling.
        iFileTreeClass.def(
            "merge",
            [](IFileTree* p, std::shared_ptr<IFileTree> other, bool returnOverwrites)
                -> std::variant<IFileTree::OverwritesType, std::size_t> {
                IFileTree::OverwritesType overwrites;
                auto result = p->merge(other, returnOverwrites ? &overwrites : nullptr);
                if (result == IFileTree::MERGE_FAILED) {
                    throw std::logic_error("merge failed");
                }
                if (returnOverwrites) {
                    return {overwrites};
                }
                return {result};
            },
            py::arg("other"), py::arg("overwrites") = false);

        // Insert and erase returns an iterator, which makes no sense in python,
        // so we convert it to bool. Erase is also renamed "remove" since
        // "erase" is very C++.
        iFileTreeClass.def(
            "insert",
            [](IFileTree* p, std::shared_ptr<FileTreeEntry> entry,
               IFileTree::InsertPolicy insertPolicy) {
                return p->insert(entry, insertPolicy) == p->end();
            },
            py::arg("entry"),
            py::arg("policy") = IFileTree::InsertPolicy::FAIL_IF_EXISTS);

        iFileTreeClass.def(
            "remove",
            [](IFileTree* p, QString name) {
                return p->erase(name).first != p->end();
            },
            py::arg("name"));
        iFileTreeClass.def(
            "remove",
            [](IFileTree* p, std::shared_ptr<FileTreeEntry> entry) {
                return p->erase(entry) != p->end();
            },
            py::arg("entry"));

        iFileTreeClass.def("move", &IFileTree::move, py::arg("entry"), py::arg("path"),
                           py::arg("policy") = IFileTree::InsertPolicy::FAIL_IF_EXISTS);
        iFileTreeClass.def(
            "copy",
            [](IFileTree* w, std::shared_ptr<FileTreeEntry> entry, QString path,
               IFileTree::InsertPolicy insertPolicy) {
                auto result = w->copy(entry, path, insertPolicy);
                if (result == nullptr) {
                    throw std::logic_error("copy failed");
                }
                return result;
            },
            py::arg("entry"), py::arg("path") = "",
            py::arg("insert_policy") = IFileTree::InsertPolicy::FAIL_IF_EXISTS);

        iFileTreeClass.def("clear", &IFileTree::clear);
        iFileTreeClass.def("removeAll", &IFileTree::removeAll, py::arg("names"));
        iFileTreeClass.def("removeIf", &IFileTree::removeIf, py::arg("filter"));

        // Special methods:
        iFileTreeClass.def("__getitem__",
                           py::overload_cast<std::size_t>(&IFileTree::at));

        iFileTreeClass.def("__iter__", [](IFileTree* tree) {
            return py::make_iterator(*tree);
        });
        iFileTreeClass.def("__len__", &IFileTree::size);
        iFileTreeClass.def(
            "__bool__", +[](const IFileTree* tree) {
                return !tree->empty();
            });
        iFileTreeClass.def(
            "__repr__", +[](const IFileTree* entry) {
                return "IFileTree(\"" + entry->name() + "\")";
            });
    }

    void add_make_tree_function(pybind11::module_& m)
    {
        m.def(
            "makeTree",
            [](mo2::detail::PyFileTree::callback_t callback)
                -> std::shared_ptr<IFileTree> {
                return std::make_shared<mo2::detail::PyFileTree>(nullptr, "", callback);
            },
            py::arg("callback") = mo2::detail::PyFileTree::callback_t{});
    }

}  // namespace mo2::python

#pragma optimize("", on)

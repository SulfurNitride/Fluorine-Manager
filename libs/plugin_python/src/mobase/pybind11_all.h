#ifndef PYTHON_PYBIND11_ALL_H
#define PYTHON_PYBIND11_ALL_H

#include <filesystem>

#include <QDir>

#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "pybind11_qt/pybind11_qt.h"

#include "pybind11_utils/functional.h"
#include "pybind11_utils/generator.h"
#include "pybind11_utils/shared_cpp_owner.h"
#include "pybind11_utils/smart_variant_wrapper.h"

#include <uibase/game_features/game_feature.h>
#include <uibase/isavegame.h>
#include <uibase/pluginrequirements.h>

namespace mo2::python {

    namespace detail {

        template <>
        struct smart_variant_converter<QString> {

            static QString from(std::filesystem::path const& path)
            {
                return QString::fromStdString(path.string());
            }

            static QString from(QFileInfo const& fileInfo)
            {
                return fileInfo.filePath();
            }

            static QString from(QDir const& dir) { return dir.path(); }
        };

        template <>
        struct smart_variant_converter<std::filesystem::path> {

            static std::filesystem::path from(QString const& value)
            {
                return {value.toStdWString()};
            }

            static std::filesystem::path from(QFileInfo const& fileInfo)
            {
                return fileInfo.filesystemFilePath();
            }

            static std::filesystem::path from(QDir const& dir)
            {
                return dir.filesystemPath();
            }
        };

        // we do not need specialization for QFileInfo and QDir because both of them can
        // be constructed from std::filesystem::path and QString already

    }  // namespace detail

    using FileWrapper      = smart_variant<QString, std::filesystem::path, QFileInfo>;
    using DirectoryWrapper = smart_variant<QString, std::filesystem::path, QDir>;

    // wrap the given function to accept FileWrapper (str | PathLike | QFileInfo) at the
    // given argument positions (or any valid positions if Is... is empty)
    //
    template <std::size_t... Is, class Fn>
    auto wrap_for_filepath(Fn&& fn)
    {
        return mo2::python::wrap_arguments<FileWrapper, Is...>(std::forward<Fn>(fn));
    }

    // wrap the given function to accept DirectoryWrapper (str | PathLike | QDir)
    // at the given argument positions (or any valid positions if Is... is empty)
    //
    template <std::size_t... Is, class Fn>
    auto wrap_for_directory(Fn&& fn)
    {
        return mo2::python::wrap_arguments<DirectoryWrapper, Is...>(
            std::forward<Fn>(fn));
    }

    // wrap a function-like object to return a FileWrapper instead of its return type,
    // useful to generate proper typing in Python
    //
    // note that QFileInfo has a __fspath__ in Python, so it is quite easy to convert
    // from "FileWrapper", a.k.a., str | os.PathLike | QFileInfo to Path by simply
    // calling Path() on the return type if necessary
    //
    // this should be combined with custom return-value in PYBIND11_OVERRIDE(_PURE), see
    // ISaveGame binding for an example
    //
    template <class Fn>
    auto wrap_return_for_filepath(Fn&& fn)
    {
        return mo2::python::wrap_return<FileWrapper>(std::forward<Fn>(fn));
    }

    // similar to wrap_return_for_filepath, except it returns a DirectoryWrapper instead
    // of its return type
    //
    // this is much less practical than wrap_return_for_filepath since QDir does not
    // expose __fspath__, so more complex things need to be done in Python, which is why
    // this should be used carefully (e.g., should not be used if the return type is
    // already QDir)
    //
    template <class Fn>
    auto wrap_return_for_directory(Fn&& fn)
    {
        return mo2::python::wrap_return<DirectoryWrapper>(std::forward<Fn>(fn));
    }

    // convert a QList to QStringList - QString must be constructible from QString
    //
    template <class T>
    QStringList toQStringList(QList<T> const& list)
    {
        static_assert(std::is_constructible_v<QString, T>,
                      "QString must be constructible from T.");
        return {list.begin(), list.end()};
    }

    // convert a QStringList to a QList - T must be constructible from QString
    //
    template <class T>
    QList<T> toQList(QStringList const& list)
    {
        static_assert(std::is_constructible_v<T, QString>,
                      "T must be constructible from QString.");
        return {list.begin(), list.end()};
    }

}  // namespace mo2::python

MO2_PYBIND11_SHARED_CPP_HOLDER(MOBase::IPluginRequirement)
MO2_PYBIND11_SHARED_CPP_HOLDER(MOBase::ISaveGame)
MO2_PYBIND11_SHARED_CPP_HOLDER(MOBase::GameFeature)

#endif

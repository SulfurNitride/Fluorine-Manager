/*
Copyright (C) 2022 Sebastian Herbord & MO2 Team. All rights reserved.

This file is part of python proxy plugin for MO

python proxy plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Python proxy plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with python proxy plugin.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "proxypython.h"

#include <filesystem>
#include <cerrno>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <QCoreApplication>
#include <QDirIterator>
#include <QMessageBox>
#include <QWidget>
#include <QtPlugin>

#include <uibase/log.h>
#include <uibase/utility.h>
#include <uibase/versioninfo.h>

namespace fs = std::filesystem;
using namespace MOBase;

// retrieve the path to the folder containing the proxy DLL
fs::path getPluginFolder()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    HMODULE hm = NULL;

    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)&getPluginFolder, &hm) == 0) {
        return {};
    }
    if (GetModuleFileName(hm, path, sizeof(path)) == 0) {
        return {};
    }

    return fs::path(path).parent_path();
#else
    Dl_info info;
    if (dladdr((void*)&getPluginFolder, &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return fs::path(info.dli_fname).parent_path();
#endif
}

ProxyPython::ProxyPython()
    : m_MOInfo{nullptr}, m_RunnerLib{nullptr}, m_Runner{nullptr},
      m_LoadFailure(FailureType::NONE)
{
}

bool ProxyPython::init(IOrganizer* moInfo)
{
    m_MOInfo = moInfo;

    if (m_MOInfo && !m_MOInfo->isPluginEnabled(this)) {
        return false;
    }

    if (QCoreApplication::applicationDirPath().contains(';')) {
        m_LoadFailure = FailureType::SEMICOLON;
        return true;
    }

    const auto pluginFolder = getPluginFolder();
    const auto pluginDataRoot =
        fs::exists(pluginFolder / "plugin_python") ? (pluginFolder / "plugin_python")
                                                   : pluginFolder;

    if (pluginFolder.empty()) {
#ifdef _WIN32
        DWORD error   = ::GetLastError();
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to resolve Python proxy directory ({}): {}", error,
                   qUtf8Printable(windowsErrorString(::GetLastError())));
#else
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to resolve Python proxy directory: {}",
                   std::strerror(errno));
#endif
        return false;
    }

    if (m_MOInfo && m_MOInfo->persistent(name(), "tryInit", false).toBool()) {
        m_LoadFailure = FailureType::INITIALIZATION;
        if (QMessageBox::question(
                parentWidget(), tr("Python Initialization failed"),
                tr("On a previous start the Python Plugin failed to initialize.\n"
                   "Do you want to try initializing python again (at the risk of "
                   "another crash)?\n "
                   "Suggestion: Select \"no\", and click the warning sign for further "
                   "help.Afterwards you have to re-enable the python plugin."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::No) {
            // we force enabled here (note: this is a persistent settings since MO2 2.4
            // or something), plugin
            // usually should not handle enabled/disabled themselves but this is a base
            // plugin so...
            m_MOInfo->setPersistent(name(), "enabled", false, true);
            return true;
        }
    }

    if (m_MOInfo) {
        m_MOInfo->setPersistent(name(), "tryInit", true);
    }

    // load the pythonrunner library, this is done in multiple steps:
    //
    // 1. we set the dlls/ subfolder (from the plugin) as the DLL directory so Windows
    // will look for DLLs in it, this is required to find the Python and libffi DLL, but
    // also the runner DLL
    //
    const auto dllPaths = pluginDataRoot / "dlls";
#ifdef _WIN32
    if (SetDllDirectoryW(dllPaths.c_str()) == 0) {
        DWORD error   = ::GetLastError();
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to add python DLL directory ({}): {}", error,
                   qUtf8Printable(windowsErrorString(::GetLastError())));
        return false;
    }
#endif

    // 2. we create the Python runner, we do not need to use ::LinkLibrary and
    // ::GetProcAddress because we use delayed load for the runner DLL (see the
    // CMakeLists.txt)
    //
    m_Runner = mo2::python::createPythonRunner();

    if (m_Runner) {
        const auto libpath = pluginDataRoot / "libs";
#ifdef _WIN32
        const std::vector<fs::path> paths{
            libpath / "pythoncore.zip", libpath,
            std::filesystem::path{IOrganizer::getPluginDataPath().toStdWString()}};
        m_Runner->initialize(paths);
#else
        // On Linux, pass plugin paths directly to the runner so interpreter startup
        // keeps Python's default stdlib paths intact (required for encodings, etc.).
        std::vector<fs::path> paths{
            libpath, std::filesystem::path{IOrganizer::getPluginDataPath().toStdWString()}};
#ifdef MO2_PYTHON_PURELIB_DIR
        if (std::strlen(MO2_PYTHON_PURELIB_DIR) > 0) {
            paths.emplace_back(MO2_PYTHON_PURELIB_DIR);
        }
#endif
#ifdef MO2_PYTHON_PLATLIB_DIR
        if (std::strlen(MO2_PYTHON_PLATLIB_DIR) > 0 &&
            std::strcmp(MO2_PYTHON_PLATLIB_DIR, MO2_PYTHON_PURELIB_DIR) != 0) {
            paths.emplace_back(MO2_PYTHON_PLATLIB_DIR);
        }
#endif
        m_Runner->initialize(paths);
#endif
    }

    if (m_MOInfo) {
        m_MOInfo->setPersistent(name(), "tryInit", false);
    }

    // reset DLL directory
#ifdef _WIN32
    SetDllDirectoryW(NULL);
#endif

    if (!m_Runner || !m_Runner->isInitialized()) {
        m_LoadFailure = FailureType::INITIALIZATION;
    }
    else {
        m_Runner->addDllSearchPath(pluginDataRoot / "dlls");
    }

    return true;
}

QString ProxyPython::name() const
{
    return "Python Proxy";
}

QString ProxyPython::localizedName() const
{
    return tr("Python Proxy");
}

QString ProxyPython::author() const
{
    return "AnyOldName3, Holt59, Silarn, Tannin";
}

QString ProxyPython::description() const
{
    return tr("Proxy Plugin to allow plugins written in python to be loaded");
}

VersionInfo ProxyPython::version() const
{
    return VersionInfo(3, 0, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> ProxyPython::settings() const
{
    return {};
}

QStringList ProxyPython::pluginList(const QDir& pluginPath) const
{
    QDir dir(pluginPath);
    dir.setFilter(dir.filter() | QDir::NoDotAndDotDot);
    QDirIterator iter(dir);

    // Note: We put python script (.py) and directory names, not the __init__.py
    // files in those since it is easier for the runner to import them.
    QStringList result;
    while (iter.hasNext()) {
        QString name   = iter.next();
        QFileInfo info = iter.fileInfo();
        const QString baseName = info.fileName();

        if (info.isFile() && name.endsWith(".py")) {
            // Compatibility shims staged on Linux; they are not MO2 plugins.
            if (baseName == "winreg.py" || baseName == "lzokay.py") {
                continue;
            }
            result.append(name);
        }
        else if (info.isDir() && QDir(info.absoluteFilePath()).exists("__init__.py")) {
            result.append(name);
        }
    }

    return result;
}

QList<QObject*> ProxyPython::load(const QString& identifier)
{
    if (!m_Runner) {
        return {};
    }
    return m_Runner->load(identifier);
}

void ProxyPython::unload(const QString& identifier)
{
    if (m_Runner) {
        return m_Runner->unload(identifier);
    }
}

std::vector<unsigned int> ProxyPython::activeProblems() const
{
    auto failure = m_LoadFailure;

    // don't know how this could happen but wth
    if (m_Runner && !m_Runner->isInitialized()) {
        failure = FailureType::INITIALIZATION;
    }

    if (failure != FailureType::NONE) {
        return {static_cast<std::underlying_type_t<FailureType>>(failure)};
    }

    return {};
}

QString ProxyPython::shortDescription(unsigned int key) const
{
    switch (static_cast<FailureType>(key)) {
    case FailureType::SEMICOLON:
        return tr("ModOrganizer path contains a semicolon");
    case FailureType::DLL_NOT_FOUND:
        return tr("Python DLL not found");
    case FailureType::INVALID_DLL:
        return tr("Invalid Python DLL");
    case FailureType::INITIALIZATION:
        return tr("Initializing Python failed");
    default:
        return tr("invalid problem key %1").arg(key);
    }
}

QString ProxyPython::fullDescription(unsigned int key) const
{
    switch (static_cast<FailureType>(key)) {
    case FailureType::SEMICOLON:
        return tr("The path to Mod Organizer (%1) contains a semicolon.<br>"
                  "While this is legal on NTFS drives, many applications do not "
                  "handle it correctly.<br>"
                  "Unfortunately MO depends on libraries that seem to fall into that "
                  "group.<br>"
                  "As a result the python plugin cannot be loaded, and the only "
                  "solution we can offer is to remove the semicolon or move MO to a "
                  "path without a semicolon.")
            .arg(QCoreApplication::applicationDirPath());
    case FailureType::DLL_NOT_FOUND:
        return tr("The Python plugin DLL was not found, maybe your antivirus deleted "
                  "it. Re-installing MO2 might fix the problem.");
    case FailureType::INVALID_DLL:
        return tr(
            "The Python plugin DLL is invalid, maybe your antivirus is blocking it. "
            "Re-installing MO2 and adding exclusions for it to your AV might fix the "
            "problem.");
    case FailureType::INITIALIZATION:
        return tr("The initialization of the Python plugin DLL failed, unfortunately "
                  "without any details.");
    default:
        return tr("invalid problem key %1").arg(key);
    }
}

bool ProxyPython::hasGuidedFix(unsigned int) const
{
    return false;
}

void ProxyPython::startGuidedFix(unsigned int) const {}

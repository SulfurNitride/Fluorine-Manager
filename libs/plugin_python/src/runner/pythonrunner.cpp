#include "pythonrunner.h"

#ifdef _WIN32
#pragma warning(disable : 4100)
#pragma warning(disable : 4996)

#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <optional>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "pybind11_qt/pybind11_qt.h"
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/stl/filesystem.h>

#include <uibase/log.h>
#include <uibase/utility.h>

#include "error.h"
#include "pythonutils.h"

using namespace MOBase;
namespace py = pybind11;

namespace mo2::python {

    /**
     *
     */
    class PythonRunner : public IPythonRunner {

    public:
        PythonRunner()  = default;
        ~PythonRunner() = default;

        QList<QObject*> load(const QString& identifier) override;
        void unload(const QString& identifier) override;

        bool initialize(std::vector<std::filesystem::path> const& pythonPaths) override;
        void addDllSearchPath(std::filesystem::path const& dllPath) override;
        bool isInitialized() const override;

    private:
        /**
         * @brief Ensure that the given folder is in sys.path.
         */
        void ensureFolderInPath(QString folder);

        /**
         * @brief Remove every occurrence of the given folder from sys.path.
         */
        void removeFolderFromPath(QString folder);

        /**
         * @brief Remove imported modules whose files belong to one of the roots.
         */
        void removeModulesFromRoots(const QList<QDir>& moduleRoots,
                                    const QString& identifier);

    private:
        // for each "identifier" (python file or python module folder), contains the
        // list of python objects - this does not keep the objects alive, it simply used
        // to unload plugins
        std::unordered_map<QString, std::vector<py::handle>> m_PythonObjects;
    };

    std::unique_ptr<IPythonRunner> createPythonRunner()
    {
        return std::make_unique<PythonRunner>();
    }

    bool PythonRunner::initialize(std::vector<std::filesystem::path> const& pythonPaths)
    {
        // we only initialize Python once for the whole lifetime of the program, even if
        // MO2 is restarted and the proxy or PythonRunner objects are deleted and
        // recreated, Python is not re-initialized
        //
        // in an ideal world, we would initialize Python here (or in the constructor)
        // and then finalize it in the destructor
        //
        // unfortunately, many library, including PyQt6, do not handle properly
        // re-initializing the Python interpreter, so we cannot do that and we keep the
        // interpreter alive
        //

        if (Py_IsInitialized()) {
            return true;
        }

        try {
            static const char* argv0 = "ModOrganizer.exe";

#ifndef _WIN32
            // Ensure libpython symbols are globally visible for extension modules
            // loaded later (_struct, PyQt6, etc.).
            //
            // We must promote the *already-loaded* libpython to RTLD_GLOBAL.
            // Using the compile-time filename (e.g. "libpython3.13.so.1.0") with
            // RTLD_NOLOAD can fail when the portable Python's SONAME differs
            // (e.g. "libpython3.13.so"), causing a second copy to be loaded and
            // making Py_IsInitialized() return false after Py_InitializeFromConfig().
            //
            // Instead, find the DSO that provides Py_IsInitialized via dladdr, then
            // re-dlopen that exact path with RTLD_GLOBAL.
            {
                Dl_info di;
                void* sym = dlsym(RTLD_DEFAULT, "Py_IsInitialized");
                if (sym && dladdr(sym, &di) && di.dli_fname) {
                    void* pyHandle =
                        dlopen(di.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
                    if (pyHandle) {
                        MOBase::log::debug(
                            "python: promoted '{}' to RTLD_GLOBAL via dladdr",
                            di.dli_fname);
                    } else {
                        // Fallback: load by full path (not NOLOAD).
                        pyHandle = dlopen(di.dli_fname, RTLD_NOW | RTLD_GLOBAL);
                        if (pyHandle) {
                            MOBase::log::debug(
                                "python: loaded '{}' with RTLD_GLOBAL (fresh)",
                                di.dli_fname);
                        } else {
                            MOBase::log::warn(
                                "python: failed to promote '{}' to RTLD_GLOBAL: {}",
                                di.dli_fname, dlerror());
                        }
                    }
                } else {
                    // Py_IsInitialized not yet in scope — libpython may not be loaded
                    // as a dependency yet.  Try the compile-time name.
#ifdef MO2_PYTHON_SHARED_LIBRARY
                    void* pyHandle =
                        dlopen(MO2_PYTHON_SHARED_LIBRARY, RTLD_NOW | RTLD_GLOBAL);
                    if (pyHandle) {
                        MOBase::log::debug(
                            "python: loaded '{}' with RTLD_GLOBAL (compile-time name)",
                            MO2_PYTHON_SHARED_LIBRARY);
                    } else {
                        MOBase::log::warn(
                            "python: failed to dlopen '{}': {}",
                            MO2_PYTHON_SHARED_LIBRARY, dlerror());
                    }
#else
                    MOBase::log::warn(
                        "python: Py_IsInitialized not found in global scope and "
                        "no compile-time library name available");
#endif
                }
            }
#endif

            // Determine Python home directory.
            // Priority: 1) <exe_dir>/python (bundled PBS Python)
            //           2) system Python (no PYTHONHOME — last resort fallback)
            QString pythonHome;

            {
                QString bundled = QCoreApplication::applicationDirPath() + "/python";
                if (QDir(bundled).exists()) {
                    pythonHome = bundled;
                    MOBase::log::info("python: using bundled Python at '{}'", pythonHome);
                } else {
                    MOBase::log::warn("python: bundled Python not found at '{}', "
                                      "falling back to system Python", bundled);
                }
            }

            std::optional<QByteArray> oldPythonHome;
            std::optional<QByteArray> oldPythonPath;
            auto restorePythonEnv = [&]() {
                if (oldPythonHome.has_value()) {
                    setenv("PYTHONHOME", oldPythonHome->constData(), 1);
                } else {
                    unsetenv("PYTHONHOME");
                }
                if (oldPythonPath.has_value()) {
                    setenv("PYTHONPATH", oldPythonPath->constData(), 1);
                } else {
                    unsetenv("PYTHONPATH");
                }
            };
            if (const char* v = std::getenv("PYTHONHOME"); v != nullptr) {
                oldPythonHome = QByteArray(v);
            }
            if (const char* v = std::getenv("PYTHONPATH"); v != nullptr) {
                oldPythonPath = QByteArray(v);
            }

            // Paths we want to prepend/append for MO2 plugin loading.
            auto paths = pythonPaths;

            // Build PYTHONPATH and optionally set PYTHONHOME.
            QStringList corePaths;

            if (!pythonHome.isEmpty()) {
                // Bundled or system Python with known prefix.
                const QDir libDir(pythonHome + "/lib");
                const auto pyDirs =
                    libDir.entryList({"python3.*"}, QDir::Dirs | QDir::NoDotAndDotDot);
                const QString pyverDir = pyDirs.isEmpty() ? QStringLiteral("python3.13")
                                                          : pyDirs.first();
                const QString stdlibDir = pythonHome + "/lib/" + pyverDir;
                const QString dynloadDir = stdlibDir + "/lib-dynload";
                const QString siteDir = stdlibDir + "/site-packages";

                corePaths = {stdlibDir, siteDir, dynloadDir};

                const QString stdlibZip = pythonHome + "/lib/python313.zip";
                if (QFile::exists(stdlibZip)) {
                    corePaths.prepend(stdlibZip);
                }
                const QString rootDynloadDir = pythonHome + "/lib-dynload";
                if (QDir(rootDynloadDir).exists()) {
                    corePaths.append(rootDynloadDir);
                }

                corePaths.append(pythonHome);
                setenv("PYTHONHOME", pythonHome.toUtf8().constData(), 1);
            }

            if (!corePaths.isEmpty()) {
                setenv("PYTHONPATH", corePaths.join(":").toUtf8().constData(), 1);
            }

            MOBase::log::debug(
                "python: calling Py_InitializeFromConfig, PYTHONHOME='{}', "
                "Py_IsInitialized before={}",
                pythonHome.isEmpty() ? "(system)" : pythonHome,
                Py_IsInitialized());

            // Use Py_InitializeFromConfig (Python 3.8+) for explicit error reporting.
            {
                PyConfig config;
                PyConfig_InitPythonConfig(&config);
                if (!pythonHome.isEmpty()) {
                    // Set config.home directly (more reliable than env for embedded use).
                    std::wstring wHome = pythonHome.toStdWString();
                    PyStatus status = PyConfig_SetString(&config, &config.home, wHome.c_str());
                    if (PyStatus_Exception(status)) {
                        MOBase::log::error(
                            "python: PyConfig_SetString(home) failed: '{}'",
                            status.err_msg ? status.err_msg : "(no message)");
                        PyConfig_Clear(&config);
                        restorePythonEnv();
                        return false;
                    }
                }
                PyStatus status = Py_InitializeFromConfig(&config);
                PyConfig_Clear(&config);
                if (PyStatus_Exception(status)) {
                    MOBase::log::error(
                        "python: Py_InitializeFromConfig failed: '{}' [in '{}']",
                        status.err_msg ? status.err_msg : "(no message)",
                        status.func ? status.func : "(no func)");
                    restorePythonEnv();
                    return false;
                }
            }

            MOBase::log::debug("python: Py_IsInitialized after={}",
                               Py_IsInitialized());

            if (!Py_IsInitialized()) {
                MOBase::log::error(
                    "failed to init python: Py_IsInitialized() returned false.");
                restorePythonEnv();
                return false;
            }

            {
                for (auto const& path : paths) {
                    ensureFolderInPath(QString::fromStdString(absolute(path).string()));
                }

                py::module_ mainModule   = py::module_::import("__main__");
                py::object mainNamespace = mainModule.attr("__dict__");
                mainNamespace["sys"]     = py::module_::import("sys");
                mainNamespace["mobase"]  = py::module_::import("mobase");

                mo2::python::configure_python_stream();
                mo2::python::configure_python_logging(mainNamespace["mobase"]);
            }

            // we need to release the GIL here - which is what this does
            //
            // when Python is initialized, the GIl is acquired, and if it is not
            // release, trying to acquire it on a different thread will deadlock
            PyEval_SaveThread();
            restorePythonEnv();

            return true;
        }
        catch (const py::error_already_set& ex) {
            MOBase::log::error("failed to init python: {}", ex.what());
            return false;
        }
    }

    void PythonRunner::addDllSearchPath(std::filesystem::path const& dllPath)
    {
        py::gil_scoped_acquire lock;
#ifdef _WIN32
        py::module_::import("os").attr("add_dll_directory")(absolute(dllPath));
#else
        // On Linux, prepend the folder to sys.path so Python extension modules
        // can be found.
        ensureFolderInPath(QString::fromStdString(absolute(dllPath).string()));
#endif
    }

    void PythonRunner::ensureFolderInPath(QString folder)
    {
        removeFolderFromPath(folder);

        py::module_ sys  = py::module_::import("sys");
        py::list sysPath = sys.attr("path");

        sysPath.insert(0, QDir::cleanPath(folder));
    }

    void PythonRunner::removeFolderFromPath(QString folder)
    {
        py::module_ sys  = py::module_::import("sys");
        py::list sysPath = sys.attr("path");

        // Loading two directory plugins with the same package name relies on the
        // selected plugin's parent directory taking precedence. Merely leaving an
        // existing entry somewhere in sys.path can make Python resolve the package
        // from an instance that was active earlier in this process.
        const auto caseSensitivity =
#ifdef _WIN32
            Qt::CaseInsensitive;
#else
            Qt::CaseSensitive;
#endif
        const QString cleanFolder = QDir::cleanPath(folder);
        for (py::ssize_t i = py::len(sysPath) - 1; i >= 0; --i) {
            const py::handle item = sysPath[i];
            if (!py::isinstance<py::str>(item)) {
                continue;
            }
            const QString existing = QDir::cleanPath(py::cast<QString>(item));
            if (existing.compare(cleanFolder, caseSensitivity) == 0) {
                if (PySequence_DelItem(sysPath.ptr(), i) != 0) {
                    throw py::error_already_set();
                }
            }
        }
    }

    void PythonRunner::removeModulesFromRoots(const QList<QDir>& moduleRoots,
                                              const QString& identifier)
    {
        if (moduleRoots.isEmpty()) {
            return;
        }

        py::object sys   = py::module_::import("sys");
        py::dict modules = sys.attr("modules");
        py::list keys    = modules.attr("keys")();

        auto pathBelongsToPlugin = [&moduleRoots](const QString& path) {
            if (path.isEmpty()) {
                return false;
            }

            for (const QDir& root : moduleRoots) {
                const QString relative = root.relativeFilePath(path);
                if (relative == "." ||
                    (!relative.startsWith("..") &&
                     !QDir::isAbsolutePath(relative))) {
                    return true;
                }
            }
            return false;
        };

        auto tryCastPath = [](const py::object& object) -> std::optional<QString> {
            if (object.is_none() || !py::isinstance<py::str>(object)) {
                return {};
            }
            return object.cast<QString>();
        };

        QStringList modulesToRemove;
        for (std::size_t i = 0; i < py::len(keys); ++i) {
            try {
                py::object key = keys[i];
                if (PyDict_Contains(modules.ptr(), key.ptr()) != 1) {
                    continue;
                }

                py::object mod = modules[key];
                bool remove    = false;
                QString removePath;

                if (PyObject_HasAttrString(mod.ptr(), "__file__")) {
                    const auto path = tryCastPath(mod.attr("__file__"));
                    if (path && pathBelongsToPlugin(*path)) {
                        remove     = true;
                        removePath = *path;
                    }
                }

                if (!remove && PyObject_HasAttrString(mod.ptr(), "__path__")) {
                    py::object paths = mod.attr("__path__");
                    for (std::size_t j = 0; j < py::len(paths); ++j) {
                        const auto path = tryCastPath(paths[py::int_(j)]);
                        if (path && pathBelongsToPlugin(*path)) {
                            remove     = true;
                            removePath = *path;
                            break;
                        }
                    }
                }

                if (remove) {
                    const QString moduleName = key.cast<QString>();
                    log::debug("Queueing module {} from {} for unload of {}.",
                               moduleName, removePath, identifier);
                    modulesToRemove.append(moduleName);
                }
            } catch (const py::error_already_set& ex) {
                MOBase::log::warn("failed to inspect python module during "
                                  "unload of {}: {}",
                                  identifier, ex.what());
            } catch (const std::exception& ex) {
                MOBase::log::warn("failed to inspect python module during "
                                  "unload of {}: {}",
                                  identifier, ex.what());
            }
        }

        std::sort(modulesToRemove.begin(), modulesToRemove.end(),
                  [](const QString& lhs, const QString& rhs) {
                      return lhs.count('.') > rhs.count('.');
                  });

        for (const auto& moduleName : modulesToRemove) {
            py::str key(moduleName.toStdString());
            if (PyDict_Contains(modules.ptr(), key.ptr()) == 1) {
                log::debug("Unloading module {} for {}.", moduleName, identifier);
                if (PyDict_DelItem(modules.ptr(), key.ptr()) != 0) {
                    PyErr_Clear();
                    log::warn("failed to remove python module {} during unload of {}",
                              moduleName, identifier);
                }
            }
        }
    }

    QList<QObject*> PythonRunner::load(const QString& identifier)
    {
        py::gil_scoped_acquire lock;

        const QFileInfo idInfo(identifier);
        const QString baseName = idInfo.fileName();
        if (baseName == "winreg.py" || baseName == "lzokay.py") {
            log::debug("Skipping Python compatibility shim '{}'.", identifier);
            return {};
        }

        const QFileInfo legacyDataInfo(
            idInfo.absoluteDir().filePath(QStringLiteral("data")));
        bool legacyDataActivated = false;

        auto retireUnclaimedLegacyData = [&] {
            if (!legacyDataActivated) {
                return;
            }

            const QString canonicalLegacyData =
                legacyDataInfo.canonicalFilePath();
            const bool claimed = std::any_of(
                m_PythonObjects.cbegin(), m_PythonObjects.cend(),
                [&](const auto& entry) {
                    const QFileInfo otherLegacyData(
                        QFileInfo(entry.first)
                            .absoluteDir()
                            .filePath(QStringLiteral("data")));
                    return !canonicalLegacyData.isEmpty() &&
                           otherLegacyData.canonicalFilePath() ==
                               canonicalLegacyData;
                });
            if (claimed) {
                return;
            }

            try {
                removeModulesFromRoots(
                    {QDir(legacyDataInfo.absoluteFilePath())}, identifier);
                removeFolderFromPath(legacyDataInfo.absoluteFilePath());
                legacyDataActivated = false;
            } catch (const std::exception& ex) {
                log::warn("failed to retire legacy Python path for {}: {}",
                          identifier, ex.what());
            }
        };

        // `pluginName` can either be a python file (single-file plugin or a folder
        // (whole module).
        //
        // For whole module, we simply add the parent folder to path, then we load
        // the module with a simple py::import, and we retrieve the associated
        // __dict__ from which we extract either createPlugin or createPlugins.
        //
        // For single file, we need to use py::eval_file, and we will use the
        // context (global variables) from __main__ (already contains mobase, and
        // other required module). Since the context is shared between called of
        // `instantiate`, we need to make sure to remove createPlugin(s) from
        // previous call.
        try {
            // Legacy portable MO2 plugins commonly keep generated Python helpers
            // next to their shared resources in plugins/data. Make that directory
            // available before evaluating either a single-file plugin or a package
            // entry point. Keep this inside the normal Python exception boundary.
            if (legacyDataInfo.isDir() && legacyDataInfo.isReadable()) {
                ensureFolderInPath(legacyDataInfo.absoluteFilePath());
                legacyDataActivated = true;
            }

            // dictionary that will contain createPlugin() or createPlugins().
            py::dict moduleDict;

            if (identifier.endsWith(".py")) {
                py::object mainModule = py::module_::import("__main__");

                // make a copy, otherwise we might end up calling the createPlugin() or
                // createPlugins() function multiple time
                py::dict moduleNamespace = mainModule.attr("__dict__").attr("copy")();

                std::string temp = ToString(identifier);
                py::eval_file(temp, moduleNamespace).is_none();
                moduleDict = moduleNamespace;
            }
            else {
                // Retrieve the module name:
                const QString moduleName = idInfo.fileName();
                QString moduleFolderPath = idInfo.canonicalFilePath();
                if (moduleFolderPath.isEmpty()) {
                    moduleFolderPath = idInfo.absoluteFilePath();
                }
                const QDir moduleFolder(moduleFolderPath);
                ensureFolderInPath(idInfo.absoluteDir().absolutePath());

                // check if the module is already loaded
                py::module_ sys = py::module_::import("sys");
                py::dict modules = sys.attr("modules");
                const py::str moduleKey(moduleName.toStdString());

                auto tryCastPath = [](const py::handle& object)
                    -> std::optional<QString> {
                    if (object.is_none() || !py::isinstance<py::str>(object)) {
                        return {};
                    }
                    return py::cast<QString>(object);
                };

                auto pathBelongsToPlugin = [&moduleFolder](const QString& path) {
                    if (path.isEmpty()) {
                        return false;
                    }

                    QFileInfo pathInfo(path);
                    QString absolutePath = pathInfo.canonicalFilePath();
                    if (absolutePath.isEmpty()) {
                        absolutePath = pathInfo.absoluteFilePath();
                    }

                    const QString relative =
                        moduleFolder.relativeFilePath(absolutePath);
                    return relative == "." ||
                           (relative != ".." && !relative.startsWith("../") &&
                            !QDir::isAbsolutePath(relative));
                };

                auto moduleBelongsToPlugin = [&](const py::handle& module) {
                    if (module.is_none()) {
                        return false;
                    }

                    if (PyObject_HasAttrString(module.ptr(), "__file__")) {
                        const auto path = tryCastPath(module.attr("__file__"));
                        if (path && pathBelongsToPlugin(*path)) {
                            return true;
                        }
                    }

                    if (PyObject_HasAttrString(module.ptr(), "__path__")) {
                        const py::object paths = module.attr("__path__");
                        for (py::ssize_t i = 0; i < py::len(paths); ++i) {
                            const auto path = tryCastPath(paths[py::int_(i)]);
                            if (path && pathBelongsToPlugin(*path)) {
                                return true;
                            }
                        }
                    }

                    return false;
                };

                if (modules.contains(moduleKey) &&
                    !moduleBelongsToPlugin(modules[moduleKey])) {
                    // The embedded interpreter outlives in-process instance
                    // switches. A package with this name may therefore still be
                    // cached from the previous instance. Remove the package and
                    // every child module before importing the selected copy.
                    const QString childPrefix = moduleName + ".";
                    const py::list keys = modules.attr("keys")();
                    QStringList modulesToRemove;
                    for (py::ssize_t i = 0; i < py::len(keys); ++i) {
                        const py::handle key = keys[i];
                        if (!py::isinstance<py::str>(key)) {
                            continue;
                        }
                        const QString name = py::cast<QString>(key);
                        if (name == moduleName || name.startsWith(childPrefix)) {
                            modulesToRemove.append(name);
                        }
                    }

                    std::sort(modulesToRemove.begin(), modulesToRemove.end(),
                              [](const QString& lhs, const QString& rhs) {
                                  return lhs.count('.') > rhs.count('.');
                              });
                    for (const auto& name : modulesToRemove) {
                        const py::str key(name.toStdString());
                        if (PyDict_Contains(modules.ptr(), key.ptr()) == 1 &&
                            PyDict_DelItem(modules.ptr(), key.ptr()) != 0) {
                            throw py::error_already_set();
                        }
                    }

                    py::module_::import("importlib").attr("invalidate_caches")();
                    log::debug(
                        "discarded cached python package '{}' before loading '{}'",
                        moduleName, identifier);
                }

                if (modules.contains(moduleKey)) {
                    py::module_ prev = modules[moduleKey];
                    py::module_(prev).reload();
                    moduleDict = prev.attr("__dict__");
                }
                else {
                    moduleDict = py::module_::import(ToString(moduleName).c_str())
                                     .attr("__dict__");
                }
            }

            if (py::len(moduleDict) == 0) {
                MOBase::log::error("No plugins found in {}.", identifier);
                return {};
            }

            // Create the plugins:
            std::vector<py::object> plugins;

            if (moduleDict.contains("createPlugin")) {
                plugins.push_back(moduleDict["createPlugin"]());
            }
            else if (moduleDict.contains("createPlugins")) {
                py::object pyPlugins = moduleDict["createPlugins"]();
                if (!py::isinstance<py::sequence>(pyPlugins)) {
                    MOBase::log::error(
                        "Plugin {}: createPlugins must return a sequence.", identifier);
                }
                else {
                    py::sequence pyList(pyPlugins);
                    size_t nPlugins = pyList.size();
                    for (size_t i = 0; i < nPlugins; ++i) {
                        plugins.push_back(pyList[i]);
                    }
                }
            }
            else {
                MOBase::log::error("Plugin {}: missing a createPlugin(s) function.",
                                   identifier);
            }

            // If we have no plugins, there was an issue, and we already logged the
            // problem:
            if (plugins.empty()) {
                retireUnclaimedLegacyData();
                return QList<QObject*>();
            }

            QList<QObject*> allInterfaceList;

            for (py::object pluginObj : plugins) {
                QList<QObject*> interfaceList = py::module_::import("mobase.private")
                                                    .attr("extract_plugins")(pluginObj)
                                                    .cast<QList<QObject*>>();

                if (interfaceList.isEmpty()) {
                    MOBase::log::error("Plugin {}: no plugin interface implemented.",
                                       identifier);
                }
                else {
                    // QObject interface holders keep the Python object alive. Record a
                    // borrowed handle only after at least one such holder exists; an
                    // empty extraction would otherwise leave a dangling handle as soon
                    // as this function's local py::object is released.
                    m_PythonObjects[identifier].push_back(pluginObj);
                }

                // Append the plugins to the main list:
                allInterfaceList.append(interfaceList);
            }

            if (allInterfaceList.isEmpty()) {
                retireUnclaimedLegacyData();
            }
            return allInterfaceList;
        }
        catch (const py::error_already_set& ex) {
            MOBase::log::error("Failed to import plugin from {}: {}", identifier,
                               ex.what());
            retireUnclaimedLegacyData();
            throw pyexcept::PythonError(ex);
        }
    }

    void PythonRunner::unload(const QString& identifier)
    {
        auto it = m_PythonObjects.find(identifier);
        if (it != m_PythonObjects.end()) {

            py::gil_scoped_acquire lock;

            QList<QDir> moduleRoots;
            bool retireLegacyDataPath = false;
            if (!identifier.endsWith(".py")) {
                moduleRoots.emplace_back(identifier);
            }

            const QFileInfo legacyDataInfo(
                QFileInfo(identifier).absoluteDir().filePath(
                    QStringLiteral("data")));
            if (legacyDataInfo.isDir()) {
                const QString canonicalLegacyData =
                    legacyDataInfo.canonicalFilePath();
                const bool sharedByAnotherPlugin = std::any_of(
                    m_PythonObjects.cbegin(), m_PythonObjects.cend(),
                    [&](const auto& entry) {
                        if (entry.first == identifier) {
                            return false;
                        }
                        const QFileInfo otherLegacyData(
                            QFileInfo(entry.first)
                                .absoluteDir()
                                .filePath(QStringLiteral("data")));
                        return !canonicalLegacyData.isEmpty() &&
                               otherLegacyData.canonicalFilePath() ==
                                   canonicalLegacyData;
                    });
                if (!sharedByAnotherPlugin) {
                    moduleRoots.emplace_back(legacyDataInfo.absoluteFilePath());
                    retireLegacyDataPath = true;
                }
            }

            removeModulesFromRoots(moduleRoots, identifier);

            if (retireLegacyDataPath) {
                removeFolderFromPath(legacyDataInfo.absoluteFilePath());
            }

            // Boost.Python does not handle cyclic garbace collection, so we need to
            // release everything hold by the objects before deleting the objects
            // themselves (done when erasing from m_PythonObjects).
            for (auto& obj : it->second) {
                obj.attr("__dict__").attr("clear")();
            }

            log::debug("Deleting {} python objects for {}.", it->second.size(),
                       identifier);
            m_PythonObjects.erase(it);
        }
    }

    bool PythonRunner::isInitialized() const
    {
        return Py_IsInitialized() != 0;
    }

}  // namespace mo2::python

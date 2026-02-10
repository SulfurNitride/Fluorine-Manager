#ifndef PYTHONRUNNER_H
#define PYTHONRUNNER_H

#include <filesystem>
#include <memory>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#ifdef RUNNER_BUILD
#define RUNNER_DLL_EXPORT Q_DECL_EXPORT
#else
#define RUNNER_DLL_EXPORT Q_DECL_IMPORT
#endif

namespace mo2::python {

    // python runner interface
    //
    class IPythonRunner {
    public:
        virtual QList<QObject*> load(const QString& identifier) = 0;
        virtual void unload(const QString& identifier)          = 0;

        // initialize Python
        //
        // pythonPaths contains the list of built-in paths for the Python library
        // (pythonxxx.zip, etc.), an empty list uses the default Python paths (e.g., the
        // PYTHONPATH environment variable)
        //
        virtual bool
        initialize(std::vector<std::filesystem::path> const& pythonPaths = {}) = 0;

        // add a DLL search path
        //
        virtual void addDllSearchPath(std::filesystem::path const& dllPath) = 0;

        // check if the runner has been initialized, i.e., initialize() has been
        // called and succeeded
        virtual bool isInitialized() const = 0;

        virtual ~IPythonRunner() {}
    };

    // create the Python runner
    //
    RUNNER_DLL_EXPORT std::unique_ptr<IPythonRunner> createPythonRunner();

}  // namespace mo2::python

#endif  // PYTHONRUNNER_H

#include "gtest/gtest.h"

#include "pythonrunner.h"

#include <QCoreApplication>

#include <uibase/iplugin.h>
#include <uibase/log.h>

#include <utility>

using namespace MOBase;

TEST(ModuleSwitch, ReplacesSameNamedPackageFromAnotherDirectory)
{
    int argc = 1;
    char applicationName[] = "module-switch-test";
    char* argv[] = {applicationName, nullptr};
    QCoreApplication application(argc, argv);

    MOBase::log::LoggerConfiguration logConfiguration;
    logConfiguration.name = "module-switch-test";
    MOBase::log::createDefault(std::move(logConfiguration));

    const auto pluginsFolder = QString(std::getenv("PLUGIN_DIR"));

    // A factory object without a supported QObject interface has no holder that
    // can keep it alive after load() returns. It must not leave a borrowed
    // PythonRunner handle for unload() to dereference.
    {
        auto runner = mo2::python::createPythonRunner();
        ASSERT_TRUE(runner->initialize());

        const QString identifier = pluginsFolder + "/no-interfaces.py";
        EXPECT_TRUE(runner->load(identifier).isEmpty());
        EXPECT_NO_THROW(runner->unload(identifier));
    }

    {
        auto runner = mo2::python::createPythonRunner();
        ASSERT_TRUE(runner->initialize());

        const auto objects =
            runner->load(pluginsFolder + "/module_switch_a/same_plugin");
        ASSERT_EQ(objects.size(), 1);
        const auto* plugin = qobject_cast<IPlugin*>(objects[0]);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->name(), "First package");
        delete objects[0];
    }

    // Python remains initialized when the runner is recreated during an
    // in-process instance switch. The second package has the same import name
    // but lives in another plugin directory and imports a same-named child.
    {
        auto runner = mo2::python::createPythonRunner();
        ASSERT_TRUE(runner->initialize());

        const auto objects =
            runner->load(pluginsFolder + "/module_switch_b/same_plugin");
        ASSERT_EQ(objects.size(), 1);
        const auto* plugin = qobject_cast<IPlugin*>(objects[0]);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->name(), "Second package");
        delete objects[0];
    }
}

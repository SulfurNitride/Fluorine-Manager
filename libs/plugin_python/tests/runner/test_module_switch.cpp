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

TEST(ModuleSwitch, LoadsAndRetiresLegacySiblingDataModules)
{
    int argc = 1;
    char applicationName[] = "legacy-data-test";
    char* argv[] = {applicationName, nullptr};
    QCoreApplication application(argc, argv);

    const auto pluginsFolder = QString(std::getenv("PLUGIN_DIR"));
    const auto firstIdentifier =
        pluginsFolder + "/legacy_instance_a/plugins/configurator.py";
    const auto secondIdentifier =
        pluginsFolder + "/legacy_instance_b/plugins/configurator.py";
    const auto noDataIdentifier =
        pluginsFolder +
        "/legacy_instance_without_data/plugins/configurator.py";
    const auto failedIdentifier =
        pluginsFolder + "/legacy_failed/plugins/configurator.py";
    const auto emptyIdentifier =
        pluginsFolder + "/legacy_empty/plugins/configurator.py";

    auto runner = mo2::python::createPythonRunner();
    ASSERT_TRUE(runner->initialize());

    auto firstObjects = runner->load(firstIdentifier);
    ASSERT_EQ(firstObjects.size(), 1);
    const auto* firstPlugin = qobject_cast<IPlugin*>(firstObjects[0]);
    ASSERT_NE(firstPlugin, nullptr);
    EXPECT_EQ(firstPlugin->name(), "First legacy data directory");
    EXPECT_NO_THROW(runner->unload(firstIdentifier));
    delete firstObjects[0];

    auto secondObjects = runner->load(secondIdentifier);
    ASSERT_EQ(secondObjects.size(), 1);
    const auto* secondPlugin = qobject_cast<IPlugin*>(secondObjects[0]);
    ASSERT_NE(secondPlugin, nullptr);
    EXPECT_EQ(secondPlugin->name(), "Second legacy data directory");
    EXPECT_NO_THROW(runner->unload(secondIdentifier));
    delete secondObjects[0];

    // The embedded interpreter survives instance changes. Once the final user
    // of an instance data directory unloads, that directory must not remain in
    // sys.path and satisfy imports for a later instance which has no such data.
    EXPECT_ANY_THROW(runner->load(noDataIdentifier));

    // A failed or empty plugin load may already have imported a helper. Retiring
    // only its sys.path entry is insufficient because Python would reuse the
    // helper from sys.modules for the next instance.
    EXPECT_ANY_THROW(runner->load(failedIdentifier));
    EXPECT_ANY_THROW(runner->load(noDataIdentifier));
    EXPECT_TRUE(runner->load(emptyIdentifier).isEmpty());
    EXPECT_ANY_THROW(runner->load(noDataIdentifier));
}

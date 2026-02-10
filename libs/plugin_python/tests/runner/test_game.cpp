#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "pythonrunner.h"

#include <QCoreApplication>

#include <uibase/iplugingame.h>

#include "MockOrganizer.h"

using namespace MOBase;

TEST(IPluginGame, Simple)
{
    const auto plugins_folder = QString(std::getenv("PLUGIN_DIR"));

    auto runner = mo2::python::createPythonRunner();
    runner->initialize();

    // load objects
    const auto objects = runner->load(plugins_folder + "/dummy-game.py");
    EXPECT_EQ(objects.size(), 1);

    // load the IPlugin
    IPluginGame* plugin = qobject_cast<IPluginGame*>(objects[0]);
    EXPECT_NE(plugin, nullptr);
}

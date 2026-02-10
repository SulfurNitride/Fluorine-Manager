#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "pythonrunner.h"

#include <QCoreApplication>

#include <uibase/ipluginfilemapper.h>
#include <uibase/iplugingame.h>

#include "MockOrganizer.h"

using namespace MOBase;

TEST(IPluginFileMapper, Simple)
{
    const auto plugins_folder = QString(std::getenv("PLUGIN_DIR"));

    auto runner = mo2::python::createPythonRunner();
    runner->initialize();

    // load objects
    const auto objects = runner->load(plugins_folder + "/dummy-filemapper.py");
    EXPECT_EQ(objects.size(), 3);

    // load the first IPluginFileMapper
    {
        IPluginFileMapper* plugin = qobject_cast<IPluginFileMapper*>(objects[0]);
        EXPECT_NE(plugin, nullptr);

        const auto m = plugin->mappings();
        EXPECT_EQ(m.size(), 2);

        EXPECT_EQ(m[0].source, "the source");
        EXPECT_EQ(m[0].destination, "the destination");
        EXPECT_EQ(m[0].isDirectory, false);
        EXPECT_EQ(m[0].createTarget, false);

        EXPECT_EQ(m[1].source, "the other source");
        EXPECT_EQ(m[1].destination, "the other destination");
        EXPECT_EQ(m[1].isDirectory, true);
        EXPECT_EQ(m[1].createTarget, false);
    }

    // load the second one (this is cast before IPluginGame so should be before)
    {
        IPluginFileMapper* plugin = qobject_cast<IPluginFileMapper*>(objects[1]);
        EXPECT_NE(plugin, nullptr);

        const auto m = plugin->mappings();
        EXPECT_EQ(m.size(), 1);

        EXPECT_EQ(m[0].source, "the source");
        EXPECT_EQ(m[0].destination, "the destination");
        EXPECT_EQ(m[0].isDirectory, false);
        EXPECT_EQ(m[0].createTarget, false);
    }

    // load the game plugin
    {
        IPluginGame* plugin = qobject_cast<IPluginGame*>(objects[2]);
        EXPECT_NE(plugin, nullptr);
    }
}

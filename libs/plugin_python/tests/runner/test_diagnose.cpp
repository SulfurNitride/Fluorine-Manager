#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "pythonrunner.h"

#include <QCoreApplication>

#include <uibase/iplugindiagnose.h>
#include <uibase/iplugingame.h>

#include "MockOrganizer.h"

using namespace MOBase;

using ::testing::ElementsAre;

TEST(IPluginDiagnose, Simple)
{
    const auto plugins_folder = QString(std::getenv("PLUGIN_DIR"));

    auto runner = mo2::python::createPythonRunner();
    runner->initialize();

    // load objects
    const auto objects = runner->load(plugins_folder + "/dummy-diagnose.py");
    EXPECT_EQ(objects.size(), 3);

    // load the first IPluginDiagnose
    {
        IPluginDiagnose* plugin = qobject_cast<IPluginDiagnose*>(objects[0]);
        EXPECT_NE(plugin, nullptr);

        ASSERT_THAT(plugin->activeProblems(), ElementsAre(1, 2));
        EXPECT_EQ(plugin->shortDescription(1), "short-1");
        EXPECT_EQ(plugin->fullDescription(1), "long-1");
        EXPECT_TRUE(plugin->hasGuidedFix(1));
        EXPECT_EQ(plugin->shortDescription(2), "short-2");
        EXPECT_EQ(plugin->fullDescription(2), "long-2");
        EXPECT_FALSE(plugin->hasGuidedFix(2));
    }

    // load the second one (this is cast before IPluginGame so should be before)
    {
        IPluginDiagnose* plugin = qobject_cast<IPluginDiagnose*>(objects[1]);
        EXPECT_NE(plugin, nullptr);

        ASSERT_THAT(plugin->activeProblems(), ElementsAre(5, 7));
        EXPECT_EQ(plugin->shortDescription(5), "short-5");
        EXPECT_EQ(plugin->fullDescription(5), "long-5");
        EXPECT_FALSE(plugin->hasGuidedFix(5));
        EXPECT_EQ(plugin->shortDescription(7), "short-7");
        EXPECT_EQ(plugin->fullDescription(7), "long-7");
        EXPECT_TRUE(plugin->hasGuidedFix(7));
    }

    // load the game plugin
    {
        IPluginGame* plugin = qobject_cast<IPluginGame*>(objects[2]);
        EXPECT_NE(plugin, nullptr);
    }
}

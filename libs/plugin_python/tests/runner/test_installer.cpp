#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <uibase/iplugininstallersimple.h>

#include "MockOrganizer.h"
#include "pythonrunner.h"

#include <QCoreApplication>

#include "DummyFileTree.h"

using namespace MOBase;

TEST(IPluginInstaller, Simple)
{
    const auto plugins_folder = QString(std::getenv("PLUGIN_DIR"));

    auto runner = mo2::python::createPythonRunner();
    runner->initialize();

    // load objects
    const auto objects = runner->load(plugins_folder + "/dummy-installer.py");
    EXPECT_EQ(objects.size(), 1);

    // load the IPlugin
    IPluginInstallerSimple* plugin = qobject_cast<IPluginInstallerSimple*>(objects[0]);
    EXPECT_NE(plugin, nullptr);

    // basic tests
    EXPECT_EQ(plugin->priority(), 10);
    EXPECT_EQ(plugin->isManualInstaller(), false);

    GuessedValue<QString> name{"default name"};
    QString version = "1.0.0";
    int nexus_id    = 12;

    // invalid tree
    {
        std::shared_ptr<IFileTree> invalid_tree =
            std::make_shared<DummyFileTree>(nullptr, "root");
        invalid_tree->addFile("some file");
        invalid_tree->addDirectory("some directory")->addFile("some other file");
        EXPECT_EQ(plugin->isArchiveSupported(invalid_tree), false);
        EXPECT_EQ(plugin->install(name, invalid_tree, version, nexus_id),
                  IPluginInstaller::RESULT_FAILED);

        // extra arguments stay the same
        EXPECT_EQ(name, "default name");
        EXPECT_EQ(version, "1.0.0");
        EXPECT_EQ(nexus_id, 12);
    }

    // valid simple tree
    {
        std::shared_ptr<IFileTree> valid_tree =
            std::make_shared<DummyFileTree>(nullptr, "root");
        const auto initial_tree = valid_tree;
        valid_tree->addFile("needed-file.txt");
        valid_tree->addFile("some file");
        valid_tree->addDirectory("some directory")->addFile("some other file");

        // valid tree
        EXPECT_EQ(plugin->isArchiveSupported(valid_tree), true);
        EXPECT_EQ(plugin->install(name, valid_tree, version, nexus_id),
                  IPluginInstaller::RESULT_NOTATTEMPTED);

        // name is not modified
        EXPECT_EQ(name, "default name");

        // tree is not modified
        EXPECT_EQ(valid_tree, initial_tree);

        // version and ID are modified
        EXPECT_EQ(version, "2.4.5");
        EXPECT_EQ(nexus_id, 33);
    }

    // "complex" tree
    {
        std::shared_ptr<IFileTree> complex_tree =
            std::make_shared<DummyFileTree>(nullptr, "root");
        const auto initial_tree   = complex_tree;
        const auto needed_file    = complex_tree->addFile("needed-file.txt");
        const auto extra_file     = complex_tree->addFile("extra-file.txt");
        const auto some_file      = complex_tree->addFile("some file");
        const auto some_directory = complex_tree->addDirectory("some directory");
        some_directory->addFile("some other file");

        // valid tree
        EXPECT_EQ(plugin->isArchiveSupported(complex_tree), true);
        EXPECT_EQ(plugin->install(name, complex_tree, version, nexus_id),
                  IPluginInstaller::RESULT_SUCCESS);

        // name is modified
        EXPECT_EQ(name, "new name");

        // tree is modified
        EXPECT_EQ(complex_tree->findDirectory("subtree")->find("needed-file.txt"),
                  needed_file);
        EXPECT_EQ(
            complex_tree->findDirectory("subtree")->findDirectory("extra-file.txt"),
            nullptr);
        EXPECT_EQ(
            complex_tree->findDirectory("subtree")->findDirectory("some directory"),
            some_directory);

        // version and ID are not modified (from previous call)
        EXPECT_EQ(version, "2.4.5");
        EXPECT_EQ(nexus_id, 33);
    }
}

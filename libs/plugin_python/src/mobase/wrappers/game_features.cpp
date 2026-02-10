#include "wrappers.h"

#include <tuple>

#include "../pybind11_all.h"

#include <uibase/ipluginlist.h>
#include <uibase/isavegameinfowidget.h>

#include <uibase/game_features/bsainvalidation.h>
#include <uibase/game_features/dataarchives.h>
#include <uibase/game_features/gameplugins.h>
#include <uibase/game_features/igamefeatures.h>
#include <uibase/game_features/localsavegames.h>
#include <uibase/game_features/moddatachecker.h>
#include <uibase/game_features/moddatacontent.h>
#include <uibase/game_features/savegameinfo.h>
#include <uibase/game_features/scriptextender.h>
#include <uibase/game_features/unmanagedmods.h>

#include "pyfiletree.h"

namespace py = pybind11;

using namespace MOBase;
using namespace pybind11::literals;

namespace mo2::python {

    class PyBSAInvalidation : public BSAInvalidation {
    public:
        bool isInvalidationBSA(const QString& bsaName) override
        {
            PYBIND11_OVERRIDE_PURE(bool, BSAInvalidation, isInvalidationBSA, bsaName);
        }
        void deactivate(MOBase::IProfile* profile) override
        {
            PYBIND11_OVERRIDE_PURE(void, BSAInvalidation, deactivate, profile);
        }
        void activate(MOBase::IProfile* profile) override
        {
            PYBIND11_OVERRIDE_PURE(void, BSAInvalidation, activate, profile);
        }
        bool prepareProfile(MOBase::IProfile* profile) override
        {
            PYBIND11_OVERRIDE_PURE(bool, BSAInvalidation, prepareProfile, profile);
        }
    };

    class PyDataArchives : public DataArchives {
    public:
        QStringList vanillaArchives() const override
        {
            PYBIND11_OVERRIDE_PURE(QStringList, DataArchives, vanillaArchives, );
        }
        QStringList archives(const MOBase::IProfile* profile) const override
        {
            PYBIND11_OVERRIDE_PURE(QStringList, DataArchives, archives, profile);
        }
        void addArchive(MOBase::IProfile* profile, int index,
                        const QString& archiveName) override
        {
            PYBIND11_OVERRIDE_PURE(void, DataArchives, addArchive, profile, index,
                                   archiveName);
        }
        void removeArchive(MOBase::IProfile* profile,
                           const QString& archiveName) override
        {
            PYBIND11_OVERRIDE_PURE(void, DataArchives, removeArchive, profile,
                                   archiveName);
        }
    };

    class PyGamePlugins : public GamePlugins {
    public:
        void writePluginLists(const MOBase::IPluginList* pluginList) override
        {
            PYBIND11_OVERRIDE_PURE(void, GamePlugins, writePluginLists, pluginList);
        }
        void readPluginLists(MOBase::IPluginList* pluginList) override
        {
            // TODO: cannot update plugin list or create one from Python so this is
            // useless
            PYBIND11_OVERRIDE_PURE(void, GamePlugins, readPluginLists, pluginList);
        }
        QStringList getLoadOrder() override
        {
            PYBIND11_OVERRIDE_PURE(QStringList, GamePlugins, getLoadOrder, );
        }
        bool lightPluginsAreSupported() override
        {
            PYBIND11_OVERRIDE(bool, GamePlugins, lightPluginsAreSupported, );
        }
        bool mediumPluginsAreSupported() override
        {
            PYBIND11_OVERRIDE(bool, GamePlugins, mediumPluginsAreSupported, );
        }
        bool blueprintPluginsAreSupported() override
        {
            PYBIND11_OVERRIDE(bool, GamePlugins, blueprintPluginsAreSupported, );
        }
    };

    class PyLocalSavegames : public LocalSavegames {
    public:
        MappingType mappings(const QDir& profileSaveDir) const override
        {
            PYBIND11_OVERRIDE_PURE(MappingType, LocalSavegames, mappings,
                                   profileSaveDir);
        }
        bool prepareProfile(MOBase::IProfile* profile) override
        {
            PYBIND11_OVERRIDE_PURE(bool, LocalSavegames, prepareProfile, profile);
        }
    };

    class PyModDataChecker : public ModDataChecker {
    public:
        CheckReturn
        dataLooksValid(std::shared_ptr<const MOBase::IFileTree> fileTree) const override
        {
            PYBIND11_OVERRIDE_PURE(CheckReturn, ModDataChecker, dataLooksValid,
                                   fileTree);
        }

        std::shared_ptr<MOBase::IFileTree>
        fix(std::shared_ptr<MOBase::IFileTree> fileTree) const override
        {
            PYBIND11_OVERRIDE(std::shared_ptr<MOBase::IFileTree>, ModDataChecker, fix,
                              fileTree);
        }
    };

    class PyModDataContent : public ModDataContent {
    public:
        std::vector<Content> getAllContents() const override
        {
            PYBIND11_OVERRIDE_PURE(std::vector<Content>, ModDataContent,
                                   getAllContents, );
            ;
        }
        std::vector<int>
        getContentsFor(std::shared_ptr<const MOBase::IFileTree> fileTree) const override
        {
            PYBIND11_OVERRIDE_PURE(std::vector<int>, ModDataContent, getContentsFor,
                                   fileTree);
        }
    };

    class PySaveGameInfo : public SaveGameInfo {
    public:
        MissingAssets getMissingAssets(MOBase::ISaveGame const& save) const override
        {
            PYBIND11_OVERRIDE_PURE(MissingAssets, SaveGameInfo, getMissingAssets,
                                   &save);
        }
        ISaveGameInfoWidget* getSaveGameWidget(QWidget* parent = 0) const override
        {
            PYBIND11_OVERRIDE_PURE(ISaveGameInfoWidget*, SaveGameInfo,
                                   getSaveGameWidget, parent);
        }
    };

    class PyScriptExtender : public ScriptExtender {
    public:
        QString BinaryName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, ScriptExtender, binaryName, );
        }

        QString PluginPath() const override
        {
            PYBIND11_OVERRIDE_PURE(DirectoryWrapper, ScriptExtender, pluginPath, );
        }

        QString loaderName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, ScriptExtender, loaderName, );
        }

        QString loaderPath() const override
        {
            PYBIND11_OVERRIDE_PURE(FileWrapper, ScriptExtender, loaderPath, );
        }

        QString savegameExtension() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, ScriptExtender, savegameExtension, );
        }

        bool isInstalled() const override
        {
            PYBIND11_OVERRIDE_PURE(bool, ScriptExtender, isInstalled, );
        }

        QString getExtenderVersion() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, ScriptExtender, getExtenderVersion, );
        }

        WORD getArch() const override
        {
            PYBIND11_OVERRIDE_PURE(WORD, ScriptExtender, getArch, );
        }
    };

    class PyUnmanagedMods : public UnmanagedMods {
    public:
        QStringList mods(bool onlyOfficial) const override
        {
            PYBIND11_OVERRIDE_PURE(QStringList, UnmanagedMods, mods, onlyOfficial);
        }
        QString displayName(const QString& modName) const override
        {
            PYBIND11_OVERRIDE_PURE(QString, UnmanagedMods, displayName, modName);
        }
        QFileInfo referenceFile(const QString& modName) const override
        {
            PYBIND11_OVERRIDE_PURE(FileWrapper, UnmanagedMods, referenceFile, modName);
        }
        QStringList secondaryFiles(const QString& modName) const override
        {
            return toQStringList([&] {
                PYBIND11_OVERRIDE_PURE(QList<FileWrapper>, UnmanagedMods,
                                       secondaryFiles, modName);
            }());
        }
    };

    void add_game_feature_bindings(pybind11::module_ m)
    {
        // this is just to allow accepting GameFeature in function, we do not expose
        // anything from game feature to Python since typeInfo() is useless in Python
        //
        py::class_<GameFeature, std::shared_ptr<GameFeature>>(m, "GameFeature");

        // BSAInvalidation

        py::class_<BSAInvalidation, GameFeature, PyBSAInvalidation,
                   std::shared_ptr<BSAInvalidation>>(m, "BSAInvalidation")
            .def(py::init<>())
            .def("isInvalidationBSA", &BSAInvalidation::isInvalidationBSA, "name"_a)
            .def("deactivate", &BSAInvalidation::deactivate, "profile"_a)
            .def("activate", &BSAInvalidation::activate, "profile"_a);

        // DataArchives

        py::class_<DataArchives, GameFeature, PyDataArchives,
                   std::shared_ptr<DataArchives>>(m, "DataArchives")
            .def(py::init<>())
            .def("vanillaArchives", &DataArchives::vanillaArchives)
            .def("archives", &DataArchives::archives, "profile"_a)
            .def("addArchive", &DataArchives::addArchive, "profile"_a, "index"_a,
                 "name"_a)
            .def("removeArchive", &DataArchives::removeArchive, "profile"_a, "name"_a);

        // GamePlugins

        py::class_<GamePlugins, GameFeature, PyGamePlugins,
                   std::shared_ptr<GamePlugins>>(m, "GamePlugins")
            .def(py::init<>())
            .def("writePluginLists", &GamePlugins::writePluginLists, "plugin_list"_a)
            .def("readPluginLists", &GamePlugins::readPluginLists, "plugin_list"_a)
            .def("getLoadOrder", &GamePlugins::getLoadOrder)
            .def("lightPluginsAreSupported", &GamePlugins::lightPluginsAreSupported)
            .def("mediumPluginsAreSupported", &GamePlugins::mediumPluginsAreSupported)
            .def("blueprintPluginsAreSupported",
                 &GamePlugins::blueprintPluginsAreSupported);

        // LocalSavegames

        py::class_<LocalSavegames, GameFeature, PyLocalSavegames,
                   std::shared_ptr<LocalSavegames>>(m, "LocalSavegames")
            .def(py::init<>())
            .def("mappings", &LocalSavegames::mappings, "profile_save_dir"_a)
            .def("prepareProfile", &LocalSavegames::prepareProfile, "profile"_a);

        // ModDataChecker

        py::class_<ModDataChecker, GameFeature, PyModDataChecker,
                   std::shared_ptr<ModDataChecker>>
            pyModDataChecker(m, "ModDataChecker");

        py::enum_<ModDataChecker::CheckReturn>(pyModDataChecker, "CheckReturn")
            .value("INVALID", ModDataChecker::CheckReturn::INVALID)
            .value("FIXABLE", ModDataChecker::CheckReturn::FIXABLE)
            .value("VALID", ModDataChecker::CheckReturn::VALID)
            .export_values();

        pyModDataChecker.def(py::init<>())
            .def("dataLooksValid", &ModDataChecker::dataLooksValid, "filetree"_a)
            .def("fix", &ModDataChecker::fix, "filetree"_a);

        // ModDataContent
        py::class_<ModDataContent, GameFeature, PyModDataContent,
                   std::shared_ptr<ModDataContent>>
            pyModDataContent(m, "ModDataContent");

        py::class_<ModDataContent::Content>(pyModDataContent, "Content")
            .def(py::init<int, QString, QString, bool>(), "id"_a, "name"_a, "icon"_a,
                 "filter_only"_a = false)
            .def_property_readonly("id", &ModDataContent::Content::id)
            .def_property_readonly("name", &ModDataContent::Content::name)
            .def_property_readonly("icon", &ModDataContent::Content::icon)
            .def("isOnlyForFilter", &ModDataContent::Content::isOnlyForFilter);

        pyModDataContent.def(py::init<>())
            .def("getAllContents", &ModDataContent::getAllContents)
            .def("getContentsFor", &ModDataContent::getContentsFor, "filetree"_a);

        // SaveGameInfo

        py::class_<SaveGameInfo, GameFeature, PySaveGameInfo,
                   std::shared_ptr<SaveGameInfo>>(m, "SaveGameInfo")
            .def(py::init<>())
            .def("getMissingAssets", &SaveGameInfo::getMissingAssets, "save"_a)
            .def("getSaveGameWidget", &SaveGameInfo::getSaveGameWidget,
                 py::return_value_policy::reference, "parent"_a);

        // ScriptExtender

        py::class_<ScriptExtender, GameFeature, PyScriptExtender,
                   std::shared_ptr<ScriptExtender>>(m, "ScriptExtender")
            .def(py::init<>())
            .def("binaryName", &ScriptExtender::BinaryName)
            .def("pluginPath", wrap_return_for_directory(&ScriptExtender::PluginPath))
            .def("loaderName", &ScriptExtender::loaderName)
            .def("loaderPath", wrap_return_for_filepath(&ScriptExtender::loaderPath))
            .def("savegameExtension", &ScriptExtender::savegameExtension)
            .def("isInstalled", &ScriptExtender::isInstalled)
            .def("getExtenderVersion", &ScriptExtender::getExtenderVersion)
            .def("getArch", &ScriptExtender::getArch);

        // UnmanagedMods

        py::class_<UnmanagedMods, GameFeature, PyUnmanagedMods,
                   std::shared_ptr<UnmanagedMods>>(m, "UnmanagedMods")
            .def(py::init<>())
            .def("mods", &UnmanagedMods::mods, "official_only"_a)
            .def("displayName", &UnmanagedMods::displayName, "mod_name"_a)
            .def("referenceFile",
                 wrap_return_for_filepath(&UnmanagedMods::referenceFile), "mod_name"_a)
            .def(
                "secondaryFiles",
                [](UnmanagedMods* m, const QString& modName) -> QList<FileWrapper> {
                    auto result = m->secondaryFiles(modName);
                    return {result.begin(), result.end()};
                },
                "mod_name"_a);
    }

    void add_igamefeatures_classes(py::module_ m)
    {
        py::class_<IGameFeatures>(m, "IGameFeatures")
            .def("registerFeature",
                 py::overload_cast<QStringList const&, std::shared_ptr<GameFeature>,
                                   int, bool>(&IGameFeatures::registerFeature),
                 "games"_a, "feature"_a, "priority"_a, "replace"_a = false)
            .def("registerFeature",
                 py::overload_cast<MOBase::IPluginGame*, std::shared_ptr<GameFeature>,
                                   int, bool>(&IGameFeatures::registerFeature),
                 "game"_a, "feature"_a, "priority"_a, "replace"_a = false)
            .def("registerFeature",
                 py::overload_cast<std::shared_ptr<GameFeature>, int, bool>(
                     &IGameFeatures::registerFeature),
                 "feature"_a, "priority"_a, "replace"_a = false)
            .def("unregisterFeature", &IGameFeatures::unregisterFeature, "feature"_a)
            .def("unregisterFeatures", &unregister_feature, "feature_type"_a)
            .def("gameFeature", &extract_feature, "feature_type"_a,
                 py ::return_value_policy::reference);
    }

}  // namespace mo2::python

namespace mo2::python {

    class GameFeaturesHelper {
        template <class F, std::size_t... Is>
        static void helper(F&& f, std::index_sequence<Is...>)
        {
            (f(static_cast<
                 std::tuple_element_t<Is, MOBase::details::BaseGameFeaturesP>>(
                 nullptr)),
             ...);
        }

    public:
        // apply the function f on a null-pointer of type Feature* for each game
        // feature
        template <class F>
        static void apply(F&& f)
        {
            helper(f, std::make_index_sequence<
                          std::tuple_size_v<MOBase::details::BaseGameFeaturesP>>{});
        }
    };

    pybind11::object extract_feature(IGameFeatures const& gameFeatures,
                                     pybind11::object type)
    {
        py::object py_feature = py::none();
        GameFeaturesHelper::apply([&]<class Feature>(Feature*) {
            if (py::type::of<Feature>().is(type)) {
                py_feature = py::cast(gameFeatures.gameFeature<Feature>(),
                                      py::return_value_policy::reference);
            }
        });
        return py_feature;
    }

    int unregister_feature(MOBase::IGameFeatures& gameFeatures, pybind11::object type)
    {
        int count = 0;
        GameFeaturesHelper::apply([&]<class Feature>(Feature*) {
            if (py::type::of<Feature>().is(type)) {
                count = gameFeatures.unregisterFeatures<Feature>();
            }
        });
        return count;
    }

}  // namespace mo2::python

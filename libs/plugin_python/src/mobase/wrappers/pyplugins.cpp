#include "wrappers.h"

#include <tuple>

#include "pyplugins.h"

namespace py = pybind11;
using namespace pybind11::literals;
using namespace MOBase;

namespace mo2::python {

    // this one is kind of big so it has its own function
    void add_iplugingame_bindings(pybind11::module_ m)
    {
        py::enum_<IPluginGame::LoadOrderMechanism>(m, "LoadOrderMechanism")
            .value("None", IPluginGame::LoadOrderMechanism::None)
            .value("FileTime", IPluginGame::LoadOrderMechanism::FileTime)
            .value("PluginsTxt", IPluginGame::LoadOrderMechanism::PluginsTxt)

            .value("NONE", IPluginGame::LoadOrderMechanism::None)
            .value("FILE_TIME", IPluginGame::LoadOrderMechanism::FileTime)
            .value("PLUGINS_TXT", IPluginGame::LoadOrderMechanism::PluginsTxt);

        py::enum_<IPluginGame::SortMechanism>(m, "SortMechanism")
            .value("NONE", IPluginGame::SortMechanism::NONE)
            .value("MLOX", IPluginGame::SortMechanism::MLOX)
            .value("BOSS", IPluginGame::SortMechanism::BOSS)
            .value("LOOT", IPluginGame::SortMechanism::LOOT);

        // this does not actually do the conversion, but might be convenient
        // for accessing the names for enum bits
        py::enum_<IPluginGame::ProfileSetting>(m, "ProfileSetting", py::arithmetic())
            .value("mods", IPluginGame::MODS)
            .value("configuration", IPluginGame::CONFIGURATION)
            .value("savegames", IPluginGame::SAVEGAMES)
            .value("preferDefaults", IPluginGame::PREFER_DEFAULTS)

            .value("MODS", IPluginGame::MODS)
            .value("CONFIGURATION", IPluginGame::CONFIGURATION)
            .value("SAVEGAMES", IPluginGame::SAVEGAMES)
            .value("PREFER_DEFAULTS", IPluginGame::PREFER_DEFAULTS);

        py::class_<IPluginGame, PyPluginGame, IPlugin,
                   std::unique_ptr<IPluginGame, py::nodelete>>(
            m, "IPluginGame", py::multiple_inheritance())
            .def(py::init<>())
            .def("detectGame", &IPluginGame::detectGame)
            .def("gameName", &IPluginGame::gameName)
            .def("displayGameName", &IPluginGame::displayGameName)
            .def("initializeProfile", &IPluginGame::initializeProfile, "directory"_a,
                 "settings"_a)
            .def("listSaves", &IPluginGame::listSaves, "folder"_a)
            .def("isInstalled", &IPluginGame::isInstalled)
            .def("gameIcon", &IPluginGame::gameIcon)
            .def("gameDirectory", &IPluginGame::gameDirectory)
            .def("dataDirectory", &IPluginGame::dataDirectory)
            .def("modDataDirectory", &IPluginGame::modDataDirectory)
            .def("secondaryDataDirectories", &IPluginGame::secondaryDataDirectories)
            .def("setGamePath", &IPluginGame::setGamePath, "path"_a)
            .def("documentsDirectory", &IPluginGame::documentsDirectory)
            .def("savesDirectory", &IPluginGame::savesDirectory)
            .def("executables", &IPluginGame::executables)
            .def("executableForcedLoads", &IPluginGame::executableForcedLoads)
            .def("steamAPPId", &IPluginGame::steamAPPId)
            .def("primaryPlugins", &IPluginGame::primaryPlugins)
            .def("enabledPlugins", &IPluginGame::enabledPlugins)
            .def("gameVariants", &IPluginGame::gameVariants)
            .def("setGameVariant", &IPluginGame::setGameVariant, "variant"_a)
            .def("binaryName", &IPluginGame::binaryName)
            .def("gameShortName", &IPluginGame::gameShortName)
            .def("lootGameName", &IPluginGame::lootGameName)
            .def("primarySources", &IPluginGame::primarySources)
            .def("validShortNames", &IPluginGame::validShortNames)
            .def("gameNexusName", &IPluginGame::gameNexusName)
            .def("iniFiles", &IPluginGame::iniFiles)
            .def("DLCPlugins", &IPluginGame::DLCPlugins)
            .def("CCPlugins", &IPluginGame::CCPlugins)
            .def("loadOrderMechanism", &IPluginGame::loadOrderMechanism)
            .def("sortMechanism", &IPluginGame::sortMechanism)
            .def("nexusModOrganizerID", &IPluginGame::nexusModOrganizerID)
            .def("nexusGameID", &IPluginGame::nexusGameID)
            .def("looksValid", &IPluginGame::looksValid, "directory"_a)
            .def("gameVersion", &IPluginGame::gameVersion)
            .def("getLauncherName", &IPluginGame::getLauncherName)
            .def("getSupportURL", &IPluginGame::getSupportURL)
            .def("getModMappings", &IPluginGame::getModMappings);
    }

    // multiple installers
    void add_iplugininstaller_bindings(pybind11::module_ m)
    {
        // this is bind but should not be inherited in Python - does not make sense,
        // having it makes it simpler to bind the Simple and Custom installers
        py::class_<IPluginInstaller, PyPluginInstallerBase<IPluginInstaller>, IPlugin,
                   std::unique_ptr<IPluginInstaller, py::nodelete>>(
            m, "IPluginInstaller", py::multiple_inheritance())
            .def("isArchiveSupported", &IPluginInstaller::isArchiveSupported, "tree"_a)
            .def("priority", &IPluginInstaller::priority)
            .def("onInstallationStart", &IPluginInstaller::onInstallationStart,
                 "archive"_a, "reinstallation"_a, "current_mod"_a)
            .def("onInstallationEnd", &IPluginInstaller::onInstallationEnd, "result"_a,
                 "new_mod"_a)
            .def("isManualInstaller", &IPluginInstaller::isManualInstaller)
            .def("setParentWidget", &IPluginInstaller::setParentWidget, "parent"_a)
            .def("setInstallationManager", &IPluginInstaller::setInstallationManager,
                 "manager"_a)
            .def("_parentWidget",
                 &PyPluginInstallerBase<IPluginInstaller>::parentWidget)
            .def("_manager", &PyPluginInstallerBase<IPluginInstaller>::manager,
                 py::return_value_policy::reference);

        py::class_<IPluginInstallerSimple, PyPluginInstallerSimple, IPluginInstaller,
                   IPlugin, std::unique_ptr<IPluginInstallerSimple, py::nodelete>>(
            m, "IPluginInstallerSimple", py::multiple_inheritance())
            .def(py::init<>())

            // note: keeping the variant here even if we always return a tuple
            // to be consistent with the wrapper and have proper stubs generation.
            .def(
                "install",
                [](IPluginInstallerSimple* p, GuessedValue<QString>& modName,
                   std::shared_ptr<IFileTree>& tree, QString& version,
                   int& nexusID) -> PyPluginInstallerSimple::py_install_return_type {
                    auto result = p->install(modName, tree, version, nexusID);
                    return std::make_tuple(result, tree, version, nexusID);
                },
                "name"_a, "tree"_a, "version"_a, "nexus_id"_a);

        py::class_<IPluginInstallerCustom, PyPluginInstallerCustom, IPluginInstaller,
                   IPlugin, std::unique_ptr<IPluginInstallerCustom, py::nodelete>>(
            m, "IPluginInstallerCustom", py::multiple_inheritance())
            .def(py::init<>())
            .def("isArchiveSupported", &IPluginInstallerCustom::isArchiveSupported,
                 "archive_name"_a)
            .def("supportedExtensions", &IPluginInstallerCustom::supportedExtensions)
            .def("install", &IPluginInstallerCustom::install, "mod_name"_a,
                 "game_name"_a, "archive_name"_a, "version"_a, "nexus_id"_a);
    }

    void add_plugins_bindings(pybind11::module_ m)
    {
        py::class_<IPlugin, PyPlugin, std::unique_ptr<IPlugin, py::nodelete>>(
            m, "IPluginBase", py::multiple_inheritance())
            .def(py::init<>())
            .def("init", &IPlugin::init, "organizer"_a)
            .def("name", &IPlugin::name)
            .def("localizedName", &IPlugin::localizedName)
            .def("master", &IPlugin::master)
            .def("author", &IPlugin::author)
            .def("description", &IPlugin::description)
            .def("version", &IPlugin::version)
            .def("requirements", &IPlugin::requirements)
            .def("settings", &IPlugin::settings)
            .def("enabledByDefault", &IPlugin::enabledByDefault);

        py::class_<IPyPlugin, PyPlugin, IPlugin,
                   std::unique_ptr<IPyPlugin, py::nodelete>>(m, "IPlugin",
                                                             py::multiple_inheritance())
            .def(py::init<>());

        py::class_<IPyPluginFileMapper, PyPluginFileMapper, IPlugin,
                   std::unique_ptr<IPyPluginFileMapper, py::nodelete>>(
            m, "IPluginFileMapper", py::multiple_inheritance())
            .def(py::init<>())
            .def("mappings", &IPluginFileMapper::mappings);

        py::class_<IPyPluginDiagnose, PyPluginDiagnose, IPlugin,
                   std::unique_ptr<IPyPluginDiagnose, py::nodelete>>(
            m, "IPluginDiagnose", py::multiple_inheritance())
            .def(py::init<>())
            .def("activeProblems", &IPluginDiagnose::activeProblems)
            .def("shortDescription", &IPluginDiagnose::shortDescription, "key"_a)
            .def("fullDescription", &IPluginDiagnose::fullDescription, "key"_a)
            .def("hasGuidedFix", &IPluginDiagnose::hasGuidedFix, "key"_a)
            .def("startGuidedFix", &IPluginDiagnose::startGuidedFix, "key"_a)
            .def("_invalidate", &PyPluginDiagnose::invalidate);

        py::class_<IPluginTool, PyPluginTool, IPlugin,
                   std::unique_ptr<IPluginTool, py::nodelete>>(
            m, "IPluginTool", py::multiple_inheritance())
            .def(py::init<>())
            .def("displayName", &IPluginTool::displayName)
            .def("tooltip", &IPluginTool::tooltip)
            .def("icon", &IPluginTool::icon)
            .def("display", &IPluginTool::display)
            .def("setParentWidget", &IPluginTool::setParentWidget)
            .def("_parentWidget", &PyPluginTool::parentWidget);

        py::class_<IPluginPreview, PyPluginPreview, IPlugin,
                   std::unique_ptr<IPluginPreview, py::nodelete>>(
            m, "IPluginPreview", py::multiple_inheritance())
            .def(py::init<>())
            .def("supportedExtensions", &IPluginPreview::supportedExtensions)
            .def("supportsArchives", &IPluginPreview::supportsArchives)
            .def("genFilePreview", &IPluginPreview::genFilePreview, "filename"_a,
                 "max_size"_a)
            .def("genDataPreview", &IPluginPreview::genDataPreview, "file_data"_a,
                 "filename"_a, "max_size"_a);

        py::class_<IPluginModPage, PyPluginModPage, IPlugin,
                   std::unique_ptr<IPluginModPage, py::nodelete>>(
            m, "IPluginModPage", py::multiple_inheritance())
            .def(py::init<>())
            .def("displayName", &IPluginModPage::displayName)
            .def("icon", &IPluginModPage::icon)
            .def("pageURL", &IPluginModPage::pageURL)
            .def("useIntegratedBrowser", &IPluginModPage::useIntegratedBrowser)
            .def("handlesDownload", &IPluginModPage::handlesDownload, "page_url"_a,
                 "download_url"_a, "fileinfo"_a)
            .def("setParentWidget", &IPluginModPage::setParentWidget, "parent"_a)
            .def("_parentWidget", &PyPluginModPage::parentWidget);

        add_iplugingame_bindings(m);
        add_iplugininstaller_bindings(m);
    }

    struct extract_plugins_helper {
        QList<QObject*> objects;

        template <class IPluginClass>
        void append_if_instance(pybind11::object plugin_obj)
        {
            if (py::isinstance<IPluginClass>(plugin_obj)) {
                objects.append(plugin_obj.cast<IPluginClass*>());
            }
        }
    };

    QList<QObject*> extract_plugins(pybind11::object plugin_obj)
    {
        extract_plugins_helper helper;

        // we need to check the trampoline class for these since the interfaces do not
        // extend IPlugin
        helper.append_if_instance<IPyPluginFileMapper>(plugin_obj);
        helper.append_if_instance<IPyPluginDiagnose>(plugin_obj);

        helper.append_if_instance<IPluginModPage>(plugin_obj);
        helper.append_if_instance<IPluginPreview>(plugin_obj);
        helper.append_if_instance<IPluginTool>(plugin_obj);

        helper.append_if_instance<IPluginGame>(plugin_obj);

        // we need to check the two installer types because IPluginInstaller does not
        // inherit QObject, and the trampoline do not have a common ancestor
        helper.append_if_instance<IPluginInstallerSimple>(plugin_obj);
        helper.append_if_instance<IPluginInstallerCustom>(plugin_obj);

        if (helper.objects.isEmpty()) {
            helper.append_if_instance<IPyPlugin>(plugin_obj);
        }

        // tie the lifetime of the Python object to the lifetime of the QObject
        for (auto* object : helper.objects) {
            py::qt::set_qt_owner(object, plugin_obj);
        }

        return helper.objects;
    }

}  // namespace mo2::python

#ifndef PYTHON_WRAPPERS_PYPLUGINS_H
#define PYTHON_WRAPPERS_PYPLUGINS_H

#include "../pybind11_all.h"

#include <uibase/iinstallationmanager.h>

#include <uibase/iplugin.h>
#include <uibase/iplugindiagnose.h>
#include <uibase/ipluginfilemapper.h>
#include <uibase/iplugingame.h>
#include <uibase/iplugininstaller.h>
#include <uibase/iplugininstallercustom.h>
#include <uibase/iplugininstallersimple.h>
#include <uibase/ipluginmodpage.h>
#include <uibase/ipluginpreview.h>
#include <uibase/iplugintool.h>

// these needs to be defined in a header file for automoc - this file is included only
// in pyplugins.cpp
namespace mo2::python {

    using namespace MOBase;

    // we need two base trampoline because IPluginGame has some final methods.
    template <class PluginBase>
    class PyPluginBaseNoFinal : public PluginBase {
    public:
        using PluginBase::PluginBase;

        PyPluginBaseNoFinal(PyPluginBaseNoFinal const&)            = delete;
        PyPluginBaseNoFinal(PyPluginBaseNoFinal&&)                 = delete;
        PyPluginBaseNoFinal& operator=(PyPluginBaseNoFinal const&) = delete;
        PyPluginBaseNoFinal& operator=(PyPluginBaseNoFinal&&)      = delete;

        bool init(IOrganizer* organizer) override
        {
            PYBIND11_OVERRIDE_PURE(bool, PluginBase, init, organizer);
        }
        QString name() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, PluginBase, name, );
        }
        QString localizedName() const override
        {
            PYBIND11_OVERRIDE(QString, PluginBase, localizedName, );
        }
        QString master() const override
        {
            PYBIND11_OVERRIDE(QString, PluginBase, master, );
        }
        QString author() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, PluginBase, author, );
        }
        QString description() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, PluginBase, description, );
        }
        VersionInfo version() const override
        {
            PYBIND11_OVERRIDE_PURE(VersionInfo, PluginBase, version, );
        }
        QList<PluginSetting> settings() const override
        {
            PYBIND11_OVERRIDE_PURE(QList<PluginSetting>, PluginBase, settings, );
        }
    };

    template <class PluginBase>
    class PyPluginBase : public PyPluginBaseNoFinal<PluginBase> {
    public:
        using PyPluginBaseNoFinal<PluginBase>::PyPluginBaseNoFinal;

        std::vector<std::shared_ptr<const IPluginRequirement>> requirements() const
        {
            PYBIND11_OVERRIDE(std::vector<std::shared_ptr<const IPluginRequirement>>,
                              PluginBase, requirements, );
        }
        bool enabledByDefault() const override
        {
            PYBIND11_OVERRIDE(bool, PluginBase, enabledByDefault, );
        }
    };

    // these classes do not inherit IPlugin or QObject so we need intermediate class to
    // get proper bindings
    class IPyPlugin : public QObject, public IPlugin {};
    class IPyPluginFileMapper : public IPyPlugin, public IPluginFileMapper {};
    class IPyPluginDiagnose : public IPyPlugin, public IPluginDiagnose {};

    // PyXXX classes - trampoline classes for the plugins

    class PyPlugin : public PyPluginBase<IPyPlugin> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin)
    };

    class PyPluginFileMapper : public PyPluginBase<IPyPluginFileMapper> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin MOBase::IPluginFileMapper)
    public:
        MappingType mappings() const override
        {
            PYBIND11_OVERRIDE_PURE(MappingType, IPyPluginFileMapper, mappings, );
        }
    };

    class PyPluginDiagnose : public PyPluginBase<IPyPluginDiagnose> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin MOBase::IPluginDiagnose)
    public:
        std::vector<unsigned int> activeProblems() const
        {
            PYBIND11_OVERRIDE_PURE(std::vector<unsigned int>, IPyPluginDiagnose,
                                   activeProblems, );
        }

        QString shortDescription(unsigned int key) const
        {
            PYBIND11_OVERRIDE_PURE(QString, IPyPluginDiagnose, shortDescription, key);
        }

        QString fullDescription(unsigned int key) const
        {
            PYBIND11_OVERRIDE_PURE(QString, IPyPluginDiagnose, fullDescription, key);
        }

        bool hasGuidedFix(unsigned int key) const
        {
            PYBIND11_OVERRIDE_PURE(bool, IPyPluginDiagnose, hasGuidedFix, key);
        }

        void startGuidedFix(unsigned int key) const
        {
            PYBIND11_OVERRIDE_PURE(void, IPyPluginDiagnose, startGuidedFix, key);
        }

        // we need to bring this in public scope
        using IPluginDiagnose::invalidate;
    };

    class PyPluginTool : public PyPluginBase<IPluginTool> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin MOBase::IPluginTool)
    public:
        QString displayName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginTool, displayName, );
        }
        QString tooltip() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginTool, tooltip, );
        }
        QIcon icon() const override
        {
            PYBIND11_OVERRIDE_PURE(QIcon, IPluginTool, icon, );
        }
        void setParentWidget(QWidget* widget) override
        {
            PYBIND11_OVERRIDE(void, IPluginTool, setParentWidget, widget);
        }
        void display() const override
        {
            PYBIND11_OVERRIDE_PURE(void, IPluginTool, display, );
        }

        // we need to bring this in public scope
        using IPluginTool::parentWidget;
    };

    class PyPluginPreview : public PyPluginBase<IPluginPreview> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin MOBase::IPluginPreview)
    public:
        std::set<QString> supportedExtensions() const override
        {
            PYBIND11_OVERRIDE_PURE(std::set<QString>, IPluginPreview,
                                   supportedExtensions, );
        }

        bool supportsArchives() const override
        {
            PYBIND11_OVERRIDE(bool, IPluginPreview, supportsArchives, );
        }

        QWidget* genFilePreview(const QString& fileName,
                                const QSize& maxSize) const override
        {
            PYBIND11_OVERRIDE_PURE(QWidget*, IPluginPreview, genFilePreview, fileName,
                                   maxSize);
        }

        QWidget* genDataPreview(const QByteArray& fileData, const QString& fileName,
                                const QSize& maxSize) const override
        {
            PYBIND11_OVERRIDE(QWidget*, IPluginPreview, genDataPreview, fileData,
                              fileName, maxSize);
        }
    };

    class PyPluginModPage : public PyPluginBase<IPluginModPage> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin MOBase::IPluginModPage)
    public:
        QString displayName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginModPage, displayName, );
        }

        QIcon icon() const override
        {
            PYBIND11_OVERRIDE_PURE(QIcon, IPluginModPage, icon, );
        }

        QUrl pageURL() const override
        {
            PYBIND11_OVERRIDE_PURE(QUrl, IPluginModPage, pageURL, );
        }

        bool useIntegratedBrowser() const override
        {
            PYBIND11_OVERRIDE_PURE(bool, IPluginModPage, useIntegratedBrowser, );
        }

        bool handlesDownload(const QUrl& pageURL, const QUrl& downloadURL,
                             ModRepositoryFileInfo& fileInfo) const override
        {
            // TODO: cannot modify fileInfo from Python
            PYBIND11_OVERRIDE_PURE(bool, IPluginModPage, handlesDownload, pageURL,
                                   downloadURL, &fileInfo);
        }

        void setParentWidget(QWidget* widget) override
        {
            PYBIND11_OVERRIDE(void, IPluginModPage, setParentWidget, widget);
        }

        // we need to bring this in public scope
        using IPluginModPage::parentWidget;
    };

    // installers
    template <class PluginInstallerBase>
    class PyPluginInstallerBase : public PyPluginBase<PluginInstallerBase> {
    public:
        using PyPluginBase<PluginInstallerBase>::PyPluginBase;

        unsigned int priority() const override
        {
            PYBIND11_OVERRIDE_PURE(unsigned int, PluginInstallerBase, priority);
        }

        bool isManualInstaller() const override
        {
            PYBIND11_OVERRIDE_PURE(bool, PluginInstallerBase, isManualInstaller, );
        }

        void onInstallationStart(QString const& archive, bool reinstallation,
                                 IModInterface* currentMod)
        {
            PYBIND11_OVERRIDE(void, PluginInstallerBase, onInstallationStart, archive,
                              reinstallation, currentMod);
        }

        void onInstallationEnd(IPluginInstaller::EInstallResult result,
                               IModInterface* newMod)
        {
            PYBIND11_OVERRIDE(void, PluginInstallerBase, onInstallationEnd, result,
                              newMod);
        }

        bool isArchiveSupported(std::shared_ptr<const IFileTree> tree) const override
        {
            PYBIND11_OVERRIDE_PURE(bool, PluginInstallerBase, isArchiveSupported, tree);
        }

        // we need to bring these in public scope
        using PluginInstallerBase::manager;
        using PluginInstallerBase::parentWidget;
    };

    class PyPluginInstallerCustom
        : public PyPluginInstallerBase<IPluginInstallerCustom> {
        Q_OBJECT
        Q_INTERFACES(
            MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerCustom)
    public:
        bool isArchiveSupported(const QString& archiveName) const
        {
            PYBIND11_OVERRIDE_PURE(bool, IPluginInstallerCustom, isArchiveSupported,
                                   archiveName);
        }

        std::set<QString> supportedExtensions() const
        {
            PYBIND11_OVERRIDE_PURE(std::set<QString>, IPluginInstallerCustom,
                                   supportedExtensions, );
        }

        EInstallResult install(GuessedValue<QString>& modName, QString gameName,
                               const QString& archiveName, const QString& version,
                               int nexusID) override
        {
            PYBIND11_OVERRIDE_PURE(EInstallResult, IPluginInstallerCustom, install,
                                   &modName, gameName, archiveName, version, nexusID);
        }
    };

    class PyPluginInstallerSimple
        : public PyPluginInstallerBase<IPluginInstallerSimple> {
        Q_OBJECT
        Q_INTERFACES(
            MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
    public:
        using py_install_return_type =
            std::variant<IPluginInstaller::EInstallResult, std::shared_ptr<IFileTree>,
                         std::tuple<IPluginInstaller::EInstallResult,
                                    std::shared_ptr<IFileTree>, QString, int>>;

        EInstallResult install(GuessedValue<QString>& modName,
                               std::shared_ptr<IFileTree>& tree, QString& version,
                               int& nexusID) override
        {
            const auto result = [&, this]() {
                PYBIND11_OVERRIDE_PURE(py_install_return_type, IPluginInstallerSimple,
                                       install, &modName, tree, version, nexusID);
            }();

            return std::visit(
                [&tree, &version, &nexusID](auto const& t) {
                    using type = std::decay_t<decltype(t)>;
                    if constexpr (std::is_same_v<type, EInstallResult>) {
                        return t;
                    }
                    else if constexpr (std::is_same_v<type,
                                                      std::shared_ptr<IFileTree>>) {
                        tree = t;
                        return RESULT_SUCCESS;
                    }
                    else if constexpr (std::is_same_v<
                                           type, std::tuple<EInstallResult,
                                                            std::shared_ptr<IFileTree>,
                                                            QString, int>>) {
                        tree    = std::get<1>(t);
                        version = std::get<2>(t);
                        nexusID = std::get<3>(t);
                        return std::get<0>(t);
                    }
                },
                result);
        }
    };

    // game
    class PyPluginGame : public PyPluginBaseNoFinal<IPluginGame> {
        Q_OBJECT
        Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame)
    public:
        void detectGame() override
        {
            PYBIND11_OVERRIDE_PURE(void, IPluginGame, detectGame, );
        }
        QString gameName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginGame, gameName, );
        }
        QString displayGameName() const override
        {
            PYBIND11_OVERRIDE(QString, IPluginGame, displayGameName, );
        }
        void initializeProfile(const QDir& directory,
                               ProfileSettings settings) const override
        {
            PYBIND11_OVERRIDE_PURE(void, IPluginGame, initializeProfile, directory,
                                   settings);
        }
        std::vector<std::shared_ptr<const ISaveGame>>
        listSaves(QDir folder) const override
        {
            PYBIND11_OVERRIDE_PURE(std::vector<std::shared_ptr<const ISaveGame>>,
                                   IPluginGame, listSaves, folder);
        }
        bool isInstalled() const override
        {
            PYBIND11_OVERRIDE_PURE(bool, IPluginGame, isInstalled, );
        }
        QIcon gameIcon() const override
        {
            PYBIND11_OVERRIDE_PURE(QIcon, IPluginGame, gameIcon, );
        }
        QDir gameDirectory() const override
        {
            PYBIND11_OVERRIDE_PURE(QDir, IPluginGame, gameDirectory, );
        }
        QDir dataDirectory() const override
        {
            PYBIND11_OVERRIDE_PURE(QDir, IPluginGame, dataDirectory, );
        }
        QString modDataDirectory() const override
        {
            PYBIND11_OVERRIDE(QString, IPluginGame, modDataDirectory, );
        }
        QMap<QString, QDir> secondaryDataDirectories() const override
        {
            using string_dir_map = QMap<QString, QDir>;
            PYBIND11_OVERRIDE(string_dir_map, IPluginGame, secondaryDataDirectories, );
        }
        void setGamePath(const QString& path) override
        {
            PYBIND11_OVERRIDE_PURE(void, IPluginGame, setGamePath, path);
        }
        QDir documentsDirectory() const override
        {
            PYBIND11_OVERRIDE_PURE(QDir, IPluginGame, documentsDirectory, );
        }
        QDir savesDirectory() const override
        {
            PYBIND11_OVERRIDE_PURE(QDir, IPluginGame, savesDirectory, );
        }
        QList<ExecutableInfo> executables() const override
        {
            PYBIND11_OVERRIDE(QList<ExecutableInfo>, IPluginGame, executables, );
        }
        QList<ExecutableForcedLoadSetting> executableForcedLoads() const override
        {
            PYBIND11_OVERRIDE_PURE(QList<ExecutableForcedLoadSetting>, IPluginGame,
                                   executableForcedLoads, );
        }
        QString steamAPPId() const override
        {
            PYBIND11_OVERRIDE(QString, IPluginGame, steamAPPId, );
        }
        QStringList primaryPlugins() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, primaryPlugins, );
        }
        QStringList enabledPlugins() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, enabledPlugins, );
        }
        QStringList gameVariants() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, gameVariants, );
        }
        void setGameVariant(const QString& variant) override
        {
            PYBIND11_OVERRIDE_PURE(void, IPluginGame, setGameVariant, variant);
        }
        QString binaryName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginGame, binaryName, );
        }
        QString gameShortName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginGame, gameShortName, );
        }
        QString lootGameName() const override
        {
            PYBIND11_OVERRIDE(QString, IPluginGame, lootGameName, );
        }
        QStringList primarySources() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, primarySources, );
        }
        QStringList validShortNames() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, validShortNames, );
        }
        QString gameNexusName() const override
        {
            PYBIND11_OVERRIDE(QString, IPluginGame, gameNexusName, );
        }
        QStringList iniFiles() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, iniFiles, );
        }
        QStringList DLCPlugins() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, DLCPlugins, );
        }
        QStringList CCPlugins() const override
        {
            PYBIND11_OVERRIDE(QStringList, IPluginGame, CCPlugins, );
        }
        LoadOrderMechanism loadOrderMechanism() const override
        {
            PYBIND11_OVERRIDE(LoadOrderMechanism, IPluginGame, loadOrderMechanism, );
        }
        SortMechanism sortMechanism() const override
        {
            PYBIND11_OVERRIDE(SortMechanism, IPluginGame, sortMechanism, );
        }
        int nexusModOrganizerID() const override
        {
            PYBIND11_OVERRIDE(int, IPluginGame, nexusModOrganizerID, );
        }
        int nexusGameID() const override
        {
            PYBIND11_OVERRIDE_PURE(int, IPluginGame, nexusGameID, );
        }
        bool looksValid(QDir const& dir) const override
        {
            PYBIND11_OVERRIDE_PURE(bool, IPluginGame, looksValid, dir);
        }
        QString gameVersion() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginGame, gameVersion, );
        }
        QString getLauncherName() const override
        {
            PYBIND11_OVERRIDE_PURE(QString, IPluginGame, getLauncherName, );
        }
        QString getSupportURL() const override
        {
            PYBIND11_OVERRIDE(QString, IPluginGame, getSupportURL, );
        }
        QMap<QString, QStringList> getModMappings() const override
        {
            using vfs_map = QMap<QString, QStringList>;
            PYBIND11_OVERRIDE(vfs_map, IPluginGame, getModMappings, );
        }
    };

}  // namespace mo2::python

#endif

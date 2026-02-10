#ifndef INSTALLERQUICK_H
#define INSTALLERQUICK_H

#include <uibase/game_features/moddatachecker.h>
#include <uibase/iplugininstallersimple.h>

class InstallerQuick : public MOBase::IPluginInstallerSimple
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerSimple)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
  Q_PLUGIN_METADATA(IID "org.tannin.InstallerQuick")
#endif

public:
  InstallerQuick();

  // Plugin functions:
  virtual bool init(MOBase::IOrganizer* moInfo) override;
  virtual QString name() const override;
  virtual QString localizedName() const override;
  virtual QString author() const override;
  virtual QString description() const override;
  virtual MOBase::VersionInfo version() const override;
  virtual QList<MOBase::PluginSetting> settings() const override;

  // Installer functions:
  virtual unsigned int priority() const override;
  virtual bool isManualInstaller() const override;
  virtual bool
  isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;

  // Simple installer functions:
  virtual EInstallResult install(MOBase::GuessedValue<QString>& modName,
                                 std::shared_ptr<MOBase::IFileTree>& tree,
                                 QString& version, int& modID) override;

private:
  /**
   * @brief Check if the archive is a "DataText" archive.
   *
   * A "DataText" archive is defined as having exactly one folder named like the data
   * folder of the game (`dataFolderName`) and one or more text or PDF files (standard
   * package from french modding site).
   *
   * @param tree The tree to check.
   * @param dataFolderName Name of the data folder (e.g., "data" for gamebryo games).
   * @param checker The mod data checker, or a null pointer if none is available.
   *
   * @return true if the tree represents a "DataText" archive, false otherwise.
   */
  bool isDataTextArchiveTopLayer(std::shared_ptr<const MOBase::IFileTree> tree,
                                 QString const& dataFolderName,
                                 MOBase::ModDataChecker* checker) const;

  /**
   * @brief Get the base of the archive.
   *
   * The base of the archive is either a "DataText" folder (i.e., a folder containing
   * TXT or PDF files and a valid data folder), or an actual data folder.
   *
   * @param tree The tree to check.
   * @param dataFolderName Name of the data folder (e.g., "data" for gamebryo games).
   * @param checker The mod data checker, or a null pointer if none is available.
   *
   * @return the "base" of the archive, or a null pointer if none was found.
   */
  std::shared_ptr<const MOBase::IFileTree>
  getSimpleArchiveBase(std::shared_ptr<const MOBase::IFileTree> dataTree,
                       QString const& dataFolderName,
                       MOBase::ModDataChecker* checker) const;

private:
  const MOBase::IOrganizer* m_MOInfo;
};

#endif  // INSTALLERQUICK_H

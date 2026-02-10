#ifndef CSHARP_INTERFACE_H
#define CSHARP_INTERFACE_H

#include <map>

#include <uibase/ifiletree.h>
#include <uibase/iplugininstaller.h>

namespace CSharp
{

void init(MOBase::IOrganizer* moInfo);

/**
 * @brief Initialize the C# interface before starting an installation.
 *
 * @param installer The FOMOD C# installer.
 * @param manager The installation manager from the installer.
 * @param parentWidget The parent widget from the installer.
 * @param tree The archive tree.
 * @param extractedEntries A map from extracted entries to their extracted path.
 */
void beforeInstall(
    MOBase::IPlugin const* installer, MOBase::IInstallationManager* manager,
    QWidget* parentWidget, std::shared_ptr<MOBase::IFileTree> tree,
    std::map<std::shared_ptr<const MOBase::FileTreeEntry>, QString> extractedEntries);

/**
 * @brief Clear the C# interface after an installation.
 *
 * @param scriptPath Path to the script to execute.
 * @param tree Reference where the final tree will be stored (in case of success.
 *
 * @return the installation result after performing post-installation.
 */
MOBase::IPluginInstaller::EInstallResult
executeCSharpScript(QString scriptPath, std::shared_ptr<MOBase::IFileTree>& tree);

}  // namespace CSharp
#endif

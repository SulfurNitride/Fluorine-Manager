/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef IINSTALLATIONMANAGER_H
#define IINSTALLATIONMANAGER_H

#include <QList>
#include <QString>

#include "ifiletree.h"
#include "iplugininstaller.h"

namespace MOBase
{

template <typename T>
class GuessedValue;

/**
 * @brief The IInstallationManager class.
 */
class IInstallationManager
{

public:
  virtual ~IInstallationManager() {}

  /**
   * @return the extensions of archives supported by this installation manager.
   */
  virtual QStringList getSupportedExtensions() const = 0;

  /**
   * @brief Extract the specified file from the currently opened archive to a temporary
   * location.
   *
   * This method cannot be used to extract directory.
   *
   * @param entry Entry corresponding to the file to extract.
   * @param silent If true, the dialog showing extraction progress will not be shown.
   *
   * @return the absolute path to the temporary file, or an empty string if the
   *     file was not extracted.
   *
   * @note The call will fail with an exception if no archive is open (plugins deriving
   *       from IPluginInstallerSimple can rely on that, custom installers should not).
   * @note The temporary file is automatically cleaned up after the installation.
   * @note This call can be very slow if the archive is large and "solid".
   */
  virtual QString extractFile(std::shared_ptr<const FileTreeEntry> entry,
                              bool silent = false) = 0;

  /**
   * @brief Extract the specified files from the currently opened archive to a temporary
   * location.
   *
   * @param entres Entries corresponding to the files to extract.
   * @param silent If true, the dialog showing extraction progress will not be shown.
   *
   * This method cannot be used to extract directory.
   *
   * @return the list of absolute paths to the temporary files.
   *
   * @note The call will fail with an exception if no archive is open (plugins deriving
   *       from IPluginInstallerSimple can rely on that, custom installers should not).
   * @note The temporary file is automatically cleaned up after the installation.
   * @note This call can be very slow if the archive is large and "solid".
   *
   * The flatten argument is not present here while it is present in the deprecated
   * QStringList version for multiple reasons: 1) it was never used, 2) it is kind of
   * fishy because there is no way to know if a file is going to be overriden, 3) it is
   * quite easy to flatten a IFileTree and thus to given a list of entries flattened
   * (this was not possible with the QStringList version since these were based on the
   * name of the file inside the archive).
   */
  virtual QStringList
  extractFiles(std::vector<std::shared_ptr<const FileTreeEntry>> const& entries,
               bool silent = false) = 0;

  /**
   * @brief Create a new file on the disk corresponding to the given entry.
   *
   * This method can be used by installer that needs to create files that are not in the
   * original archive. At the end of the installation, if there are entries in the final
   * tree that were used to create files, the corresponding files will be moved to the
   * mod folder.
   *
   * @param entry The entry for which a temporary file should be created.
   *
   * @return the path to the created file, or an empty QString() if the file could not
   * be created.
   */
  virtual QString createFile(std::shared_ptr<const MOBase::FileTreeEntry> entry) = 0;

  /**
   * @brief Installs the given archive.
   *
   * @param modName Suggested name of the mod.
   * @param archiveFile Path to the archive to install.
   * @param modId ID of the mod, if available.
   *
   * @return the installation result.
   */
  virtual IPluginInstaller::EInstallResult
  installArchive(MOBase::GuessedValue<QString>& modName, const QString& archiveFile,
                 int modID = 0) = 0;
};

}  // namespace MOBase

#endif  // IINSTALLATIONMANAGER_H

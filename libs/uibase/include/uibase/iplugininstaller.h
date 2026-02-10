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

#ifndef IPLUGININSTALLER_H
#define IPLUGININSTALLER_H

#include <set>

#include "ifiletree.h"
#include "imodinterface.h"
#include "iplugin.h"

namespace MOBase
{

class IInstallationManager;

class IPluginInstaller : public IPlugin
{

  Q_INTERFACES(IPlugin)

public:
  enum EInstallResult
  {
    RESULT_SUCCESS,
    RESULT_FAILED,
    RESULT_CANCELED,
    RESULT_MANUALREQUESTED,
    RESULT_NOTATTEMPTED,
    RESULT_SUCCESSCANCEL,
    RESULT_CATEGORYREQUESTED
  };

public:
  IPluginInstaller() : m_ParentWidget(nullptr), m_InstallationManager(nullptr) {}

  /**
   * @brief Retrieve the priority of this installer. If multiple installers are able
   * to handle an archive, the one with the highest priority wins.
   *
   * @return the priority of this installer.
   */
  virtual unsigned int priority() const = 0;

  /**
   * @return true if this plugin should be treated as a manual installer if the user
   * explicitly requested one. A manual installer should offer the user maximum amount
   * of customizability.
   */
  virtual bool isManualInstaller() const = 0;

  /**
   * @brief Method calls at the start of the installation process, before any other
   * methods. This method is only called once per installation process, even for
   * recursive installations (e.g. with the bundle installer).
   *
   * @param archive Path to the archive that is going to be installed.
   * @param reinstallation True if this is a reinstallation, false otherwise.
   * @param mod A currently installed mod corresponding to the archive being installed,
   * or a null if there is no such mod.
   *
   * @note If `reinstallation` is true, then the given mod is the mod being reinstalled
   * (the one selected by the user). If `reinstallation` is false and `currentMod` is
   * not null, then it corresponds to a mod MO2 thinks corresponds to the archive (e.g.
   * based on matching Nexus ID or name).
   * @note The default implementation does nothing.
   */
  virtual void onInstallationStart([[maybe_unused]] QString const& archive,
                                   [[maybe_unused]] bool reinstallation,
                                   [[maybe_unused]] IModInterface* currentMod)
  {}

  /**
   * @brief Method calls at the end of the installation process. This method is only
   * called once per installation process, even for recursive installations (e.g. with
   * the bundle installer).
   *
   * @param result The result of the installation.
   * @param mod If the installation succeeded (result is RESULT_SUCCESS), contains the
   * newly installed mod, otherwise it contains a null pointer.
   *
   * @note The default implementation does nothing.
   */
  virtual void onInstallationEnd([[maybe_unused]] EInstallResult result,
                                 [[maybe_unused]] IModInterface* newMod)
  {}

  /**
   * @brief Test if the archive represented by the tree parameter can be installed
   * through this installer.
   *
   * @param tree a directory tree representing the archive.
   *
   * @return true if this installer can handle the archive.
   */
  virtual bool isArchiveSupported(std::shared_ptr<const IFileTree> tree) const = 0;

  /**
   * @brief Sets the widget that the tool should use as the parent whenever
   *        it creates a new modal dialog.
   *
   * @param widget The new parent widget.
   */
  virtual void setParentWidget(QWidget* widget) { m_ParentWidget = widget; }

  /**
   * @brief Sets the installation manager responsible for the installation process
   * it can be used by plugins to access utility functions.
   *
   * @param manager The new installation manager.
   */
  void setInstallationManager(IInstallationManager* manager)
  {
    m_InstallationManager = manager;
  }

protected:
  /**
   * @return the parent widget that the tool should use to create new dialogs and
   * widgets.
   */
  QWidget* parentWidget() const { return m_ParentWidget; }

  /**
   * @return the manager responsible for the installation process.
   */
  IInstallationManager* manager() const { return m_InstallationManager; }

private:
  QWidget* m_ParentWidget;
  IInstallationManager* m_InstallationManager;
};

}  // namespace MOBase

Q_DECLARE_INTERFACE(MOBase::IPluginInstaller,
                    "com.tannin.ModOrganizer.PluginInstaller/1.0")

#endif  // IPLUGININSTALLER_H

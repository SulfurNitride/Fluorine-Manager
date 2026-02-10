/*
Copyright (C) 2020 Holt59. All rights reserved.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <uibase/iinstallationmanager.h>

#include "csharp_interface.h"
#include "installer_fomod_csharp.h"
#include "installer_fomod_predialog.h"
#include "xml_info_reader.h"

using namespace MOBase;

bool InstallerFomodCSharp::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  CSharp::init(moInfo);
  return true;
}

std::shared_ptr<const IFileTree>
InstallerFomodCSharp::findFomodDirectory(std::shared_ptr<const IFileTree> tree) const
{
  auto entry = tree->find("fomod", FileTreeEntry::DIRECTORY);

  if (entry != nullptr) {
    return entry->astree();
  }

  if (tree->empty()) {
    return nullptr;
  }

  // We need at least a directory:
  if (!tree->at(0)->isDir()) {
    return nullptr;
  }

  // But not two:
  if (tree->size() > 1 && tree->at(1)->isDir()) {
    return nullptr;
  }

  return findFomodDirectory(tree->at(0)->astree());
}

std::shared_ptr<const FileTreeEntry>
InstallerFomodCSharp::findScriptFile(std::shared_ptr<const IFileTree> tree) const
{
  auto fomodDirectory = findFomodDirectory(tree);

  if (fomodDirectory == nullptr) {
    return nullptr;
  }

  for (auto e : *fomodDirectory) {
    if (e->isFile() && e->suffix().compare("cs", Qt::CaseInsensitive) == 0) {
      return e;
    }
  }

  return nullptr;
}

std::shared_ptr<const FileTreeEntry>
InstallerFomodCSharp::findInfoFile(std::shared_ptr<const IFileTree> tree) const
{
  auto fomodDirectory = findFomodDirectory(tree);

  if (fomodDirectory == nullptr) {
    return nullptr;
  }

  for (auto e : *fomodDirectory) {
    if (e->isFile() && e->compare("info.xml") == 0) {
      return e;
    }
  }

  return nullptr;
}

bool InstallerFomodCSharp::isArchiveSupported(
    std::shared_ptr<const MOBase::IFileTree> tree) const
{
  return findScriptFile(tree) != nullptr;
}

InstallerFomodCSharp::EInstallResult
InstallerFomodCSharp::install(MOBase::GuessedValue<QString>& modName,
                              std::shared_ptr<MOBase::IFileTree>& tree,
                              QString& version, int& modID)
{
  static std::set<QString, FileNameComparator> imageSuffixes{"png", "jpg", "jpeg",
                                                             "gif", "bmp"};

  // Extract the script file:
  auto scriptFile = findScriptFile(tree);
  if (scriptFile == nullptr) {
    return EInstallResult::RESULT_NOTATTEMPTED;
  }

  // Check if there is a info.xml:
  auto infoFile = findInfoFile(tree);

  // Set containing everything to extract except the script and the info file:
  std::set<std::shared_ptr<const FileTreeEntry>> toExtractSet{scriptFile};

  if (infoFile != nullptr) {
    toExtractSet.insert(infoFile);
  }

  // Extract all the images:
  tree->walk([&](const QString&, auto entry) {
    if (entry->isFile() && imageSuffixes.count(entry->suffix()) > 0) {
      toExtractSet.insert(entry);
    }
    return IFileTree::WalkReturn::CONTINUE;
  });

  // Extract everything from the fomod/ folder:
  auto fomodFolder = findFomodDirectory(tree);
  fomodFolder->walk([&](const QString&, auto entry) {
    if (entry->isFile()) {
      toExtractSet.insert(entry);
    }
    return IFileTree::WalkReturn::CONTINUE;
  });

  // Convert to vector:
  std::vector toExtract(std::begin(toExtractSet), std::end(toExtractSet));
  QStringList paths(manager()->extractFiles(toExtract));

  // If user cancelled:
  if (toExtract.size() != static_cast<std::size_t>(paths.size())) {
    return EInstallResult::RESULT_CANCELED;
  }

  // Create a map from entry to file path:
  std::map<std::shared_ptr<const FileTreeEntry>, QString> entryToPath;
  for (std::size_t i = 0; i < toExtract.size(); ++i) {
    entryToPath[toExtract[i]] = paths[i];
  }

  if (infoFile != nullptr) {
    QFile file(entryToPath[infoFile]);
    if (file.open(QIODevice::ReadOnly)) {
      auto info = FomodInfoReader::readXml(file, &FomodInfoReader::parseInfo);
      if (!std::get<0>(info).isEmpty()) {
        modName.update(std::get<0>(info), GUESS_META);
      }
      if (std::get<1>(info) != -1) {
        modID = std::get<1>(info);
      }
      if (!std::get<2>(info).isEmpty()) {
        version = std::get<2>(info);
      }
    }
  }

  // Show the dialog:
  InstallerFomodPredialog dialog(modName, parentWidget());
  if (dialog.exec() != QDialog::Accepted) {
    if (dialog.manualRequested()) {
      modName.update(dialog.getName(), GUESS_USER);
      return EInstallResult::RESULT_MANUALREQUESTED;
    } else {
      return EInstallResult::RESULT_CANCELED;
    }
  }
  modName.update(dialog.getName(), GUESS_USER);

  // Run the C# script:
  const QString scriptPath = entryToPath[scriptFile];
  CSharp::beforeInstall(
      this, manager(), parentWidget(),
      std::const_pointer_cast<IFileTree>(scriptFile->parent()->parent()),
      std::move(entryToPath));
  return CSharp::executeCSharpScript(scriptPath, tree);
}

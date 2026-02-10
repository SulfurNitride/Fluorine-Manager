#include "oblivionmoddatachecker.h"

MOBase::ModDataChecker::CheckReturn OblivionModDataChecker::dataLooksValid(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  // Check with Gamebryo stuff:
  auto check = GamebryoModDataChecker::dataLooksValid(fileTree);
  if (check == CheckReturn::VALID) {
    return check;
  }

  // Check for OBSE_ files:
  for (auto const& entry : *fileTree) {
    if (entry->isDir() || !entry->name().startsWith("OBSE", Qt::CaseInsensitive)) {
      return CheckReturn::INVALID;
    }
  }

  return CheckReturn::FIXABLE;
}

std::shared_ptr<MOBase::IFileTree>
OblivionModDataChecker::fix(std::shared_ptr<MOBase::IFileTree> fileTree) const
{
  // If we arrive here, it means all files starts with OBSE.
  auto data = fileTree->createOrphanTree();
  auto obse = data->addDirectory("OBSE/Plugins");
  obse->merge(fileTree);
  return data;
}

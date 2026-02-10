#include <ifiletree.h>

#include "gamebryomoddatachecker.h"

/**
 * @return the list of possible folder names in data.
 */
auto GamebryoModDataChecker::possibleFolderNames() const -> const FileNameSet&
{
  static FileNameSet result{"fonts",
                            "interface",
                            "menus",
                            "meshes",
                            "music",
                            "scripts",
                            "shaders",
                            "sound",
                            "strings",
                            "textures",
                            "trees",
                            "video",
                            "facegen",
                            "materials",
                            "skse",
                            "obse",
                            "mwse",
                            "nvse",
                            "fose",
                            "f4se",
                            "distantlod",
                            "asi",
                            "SkyProc Patchers",
                            "Tools",
                            "MCM",
                            "icons",
                            "bookart",
                            "distantland",
                            "mits",
                            "splash",
                            "dllplugins",
                            "CalienteTools",
                            "NetScriptFramework",
                            "shadersfx"};
  return result;
}

/**
 * @return the extensions of possible files in data.
 */
auto GamebryoModDataChecker::possibleFileExtensions() const -> const FileNameSet&
{
  static FileNameSet result{"esp", "esm", "esl", "bsa", "ba2", "modgroups", "ini"};
  return result;
}

GamebryoModDataChecker::GamebryoModDataChecker(const GameGamebryo* game) : m_Game(game)
{}

GamebryoModDataChecker::CheckReturn GamebryoModDataChecker::dataLooksValid(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  auto& folders  = possibleFolderNames();
  auto& suffixes = possibleFileExtensions();
  for (auto entry : *fileTree) {
    if (entry->isDir()) {
      if (folders.count(entry->name()) > 0) {
        return CheckReturn::VALID;
      }
    } else {
      if (suffixes.count(entry->suffix()) > 0) {
        return CheckReturn::VALID;
      }
    }
  }
  return CheckReturn::INVALID;
}

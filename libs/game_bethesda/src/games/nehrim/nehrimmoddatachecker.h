#ifndef NEHRIM_MODATACHECKER_H
#define NEHRIM_MODATACHECKER_H

#include <gamebryomoddatachecker.h>

class NehrimModDataChecker : public GamebryoModDataChecker
{
public:
  using GamebryoModDataChecker::GamebryoModDataChecker;

  CheckReturn
  dataLooksValid(std::shared_ptr<const MOBase::IFileTree> fileTree) const override;
  std::shared_ptr<MOBase::IFileTree>
  fix(std::shared_ptr<MOBase::IFileTree> fileTree) const override;

protected:
  virtual const FileNameSet& possibleFolderNames() const override
  {
    static FileNameSet result{"fonts",      "interface",     "menus",
                              "meshes",     "music",         "scripts",
                              "shaders",    "sound",         "strings",
                              "textures",   "trees",         "video",
                              "facegen",    "obse",          "distantlod",
                              "asi",        "distantland",   "mits",
                              "dllplugins", "CalienteTools", "NetScriptFramework"};
    return result;
  }
  virtual const FileNameSet& possibleFileExtensions() const override
  {
    static FileNameSet result{"esp", "esm", "bsa", "modgroups", "ini"};
    return result;
  }
};

#endif  // NEHRIM_MODATACHECKER_H

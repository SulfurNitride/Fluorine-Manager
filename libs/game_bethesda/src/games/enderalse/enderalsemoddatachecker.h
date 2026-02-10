#ifndef ENDERALSE_MODATACHECKER_H
#define ENDERALSE_MODATACHECKER_H

#include <gamebryomoddatachecker.h>

class EnderalSEModDataChecker : public GamebryoModDataChecker
{
public:
  using GamebryoModDataChecker::GamebryoModDataChecker;

protected:
  virtual const FileNameSet& possibleFolderNames() const override
  {
    static FileNameSet result{
        "fonts",     "interface",     "menus",         "meshes",
        "music",     "scripts",       "shaders",       "sound",
        "strings",   "textures",      "trees",         "video",
        "facegen",   "materials",     "skse",          "distantlod",
        "asi",       "Tools",         "MCM",           "distantland",
        "mits",      "dllplugins",    "CalienteTools", "NetScriptFramework",
        "shadersfx", "Nemesis_Engine"};
    return result;
  }
  virtual const FileNameSet& possibleFileExtensions() const override
  {
    static FileNameSet result{"esp", "esm", "esl", "bsa", "modgroups", "ini"};
    return result;
  }
};

#endif  // SKYRIMSE_MODATACHECKER_H

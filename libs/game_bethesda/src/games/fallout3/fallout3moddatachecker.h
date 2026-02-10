#ifndef FALLOUT3_MODATACHECKER_H
#define FALLOUT3_MODATACHECKER_H

#include <gamebryomoddatachecker.h>

class Fallout3ModDataChecker : public GamebryoModDataChecker
{
public:
  using GamebryoModDataChecker::GamebryoModDataChecker;

protected:
  virtual const FileNameSet& possibleFolderNames() const override
  {
    static FileNameSet result{
        "fonts",    "interface",  "menus",         "meshes",
        "music",    "scripts",    "shaders",       "sound",
        "strings",  "textures",   "trees",         "video",
        "facegen",  "materials",  "fose",          "distantlod",
        "asi",      "Tools",      "MCM",           "distantland",
        "mits",     "dllplugins", "CalienteTools", "NetScriptFramework",
        "shadersfx"};
    return result;
  }
  virtual const FileNameSet& possibleFileExtensions() const override
  {
    static FileNameSet result{"esp", "esm", "bsa", "modgroups", "ini"};
    return result;
  }
};

#endif  // FALLOUT3_MODATACHECKER_H

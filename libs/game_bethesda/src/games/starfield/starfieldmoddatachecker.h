#ifndef STARFIELD_MODATACHECKER_H
#define STARFIELD_MODATACHECKER_H

#include <gamebryomoddatachecker.h>

class StarfieldModDataChecker : public GamebryoModDataChecker
{
public:
  using GamebryoModDataChecker::GamebryoModDataChecker;

protected:
  virtual const FileNameSet& possibleFolderNames() const override
  {
    static FileNameSet result{
        "interface",  "meshes",        "geometries", "music", "scripts",     "sound",
        "strings",    "textures",      "trees",      "video", "materials",   "sfse",
        "distantlod", "asi",           "Tools",      "MCM",   "distantland", "mits",
        "dllplugins", "CalienteTools", "shadersfx",  "aaf"};
    return result;
  }
  virtual const FileNameSet& possibleFileExtensions() const override
  {
    static FileNameSet result{"esp",       "esm", "esl", "ba2",
                              "modgroups", "ini", "csg", "cdx"};
    return result;
  }
};

#endif  // STARFIELD_MODATACHECKER_H

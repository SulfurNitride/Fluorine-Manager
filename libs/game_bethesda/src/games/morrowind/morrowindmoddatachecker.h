#ifndef MORROWIND_MODATACHECKER_H
#define MORROWIND_MODATACHECKER_H

#include <gamebryomoddatachecker.h>

class MorrowindModDataChecker : public GamebryoModDataChecker
{
public:
  using GamebryoModDataChecker::GamebryoModDataChecker;

protected:
  virtual const FileNameSet& possibleFolderNames() const override
  {
    static FileNameSet result{"fonts",    "meshes",  "music",  "shaders",     "sound",
                              "textures", "video",   "mwse",   "distantland", "mits",
                              "icons",    "bookart", "splash", "animations"};
    return result;
  }
  virtual const FileNameSet& possibleFileExtensions() const override
  {
    static FileNameSet result{"esp", "esm", "bsa", "modgroups"};
    return result;
  }
};

#endif  // MORROWIND_MODATACHECKER_H

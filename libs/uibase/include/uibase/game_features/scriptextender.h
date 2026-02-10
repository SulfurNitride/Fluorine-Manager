#ifndef UIBASE_GAMEFEATURES_SCRIPTEXTENDER
#define UIBASE_GAMEFEATURES_SCRIPTEXTENDER

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
using WORD = uint16_t;
#endif

#include <QString>
#include <QStringList>

#include "./game_feature.h"

namespace MOBase
{

class ScriptExtender : public details::GameFeatureCRTP<ScriptExtender>
{

public:
  virtual ~ScriptExtender() {}

  /** Get the name of the script extender binary */
  virtual QString BinaryName() const = 0;

  /** Get the script extender plugin path*/
  virtual QString PluginPath() const = 0;

  /** The loader to use to ensure the game runs with the script extender */
  virtual QString loaderName() const = 0;

  /** Full path of the loader */
  virtual QString loaderPath() const = 0;

  /** Extension of the script extender save game */
  virtual QString savegameExtension() const = 0;

  /** Returns true if the extender is installed */
  virtual bool isInstalled() const = 0;

  /** Get version of extender */
  virtual QString getExtenderVersion() const = 0;

  /** Get CPU platform of extender */
  virtual WORD getArch() const = 0;
};

}  // namespace MOBase

#endif  // SCRIPTEXTENDER

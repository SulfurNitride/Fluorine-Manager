#ifndef FLUORINECONFIG_H
#define FLUORINECONFIG_H

#include <QString>
#include <cstdint>
#include <optional>

class FluorineConfig
{
public:
  uint32_t app_id = 0;
  QString prefix_path;
  QString proton_name;
  QString proton_path;
  QString created;

  static QString configFilePath();
  static std::optional<FluorineConfig> load();

  bool save() const;
  static void deleteConfig() ;
  bool prefixExists() const;
  QString compatDataPath() const;
  bool markPrefixOwned() const;
  bool canDestroyPrefix() const;
  bool resetPrefixForRecreation() const;
  bool destroyPrefix() const;

  static bool isSetup();
  static std::optional<QString> prefixPath();

};

#endif  // FLUORINECONFIG_H

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

  static std::optional<FluorineConfig> load();

  bool save() const;
  void deleteConfig() const;
  bool prefixExists() const;
  QString compatDataPath() const;
  void destroyPrefix() const;

  static bool isSetup();
  static std::optional<QString> prefixPath();

private:
  static QString configFilePath();
};

#endif  // FLUORINECONFIG_H

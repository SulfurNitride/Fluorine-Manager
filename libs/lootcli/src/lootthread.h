#ifndef LOOTTHREAD_H
#define LOOTTHREAD_H

#include "game_settings.h"
#include "loot/database_interface.h"
#include <lootcli/lootcli.h>

namespace loot
{
class Game;
}

namespace lootcli
{

loot::LogLevel toLootLogLevel(lootcli::LogLevels level);
lootcli::LogLevels fromLootLogLevel(loot::LogLevel level);

class LOOTWorker
{
public:
  explicit LOOTWorker();

  void setGame(const std::string& gameName);
  void setGamePath(const std::string& gamePath);
  void setOutput(const std::string& outputPath);
  void setPluginListPath(const std::string& pluginListPath);
  void
  setLanguageCode(const std::string& language_code);  // Will add this when I figure out
                                                      // how languages work on MO
  void setLogLevel(loot::LogLevel level);

  void setUpdateMasterlist(bool update);

  int run();

private:
  void progress(Progress p);
  void log(loot::LogLevel level, const std::string_view message) const;

#ifdef _WIN32
  DWORD GetFile(const WCHAR* szUrl, const CHAR* szFileName);
#else
  int GetFile(const std::string& url, const std::string& fileName);
#endif
  void getSettings(const std::filesystem::path& file);
  std::string getOldDefaultRepoUrl(loot::GameId gameType);
  std::optional<std::string> GetLocalFolder(const toml::table& table);
  bool IsNehrim(const toml::table& table);
  bool IsEnderal(const toml::table& table, const std::string& expectedLocalFolder);
  bool IsEnderal(const toml::table& table);
  bool IsEnderalSE(const toml::table& table);
  bool isLocalPath(const std::string& location, const std::string& filename);
  bool isBranchCheckedOut(const std::filesystem::path& localGitRepo,
                          const std::string& branch);
  std::optional<std::string> migrateMasterlistRepoSettings(loot::GameId gameType,
                                                           std::string url,
                                                           std::string branch);
  std::string migrateMasterlistSource(const std::string& source);

  std::filesystem::path gamePath() const;
  std::filesystem::path masterlistPath() const;
  std::filesystem::path settingsPath() const;
  std::filesystem::path userlistPath() const;
  std::filesystem::path l10nPath() const;
  std::filesystem::path dataPath() const;

private:
  // void handleErr(unsigned int resultCode, const char *description);
  bool sort(loot::Game& game);
  // const char *lootErrorString(unsigned int errorCode);
  // template <typename T> T resolveVariable(HMODULE lib, const char *name);
  // template <typename T> T resolveFunction(HMODULE lib, const char *name);

private:
  loot::GameId m_GameId;
  std::string m_Language;
  std::string m_GameName;
  std::string m_GamePath;
  std::string m_OutputPath;
  std::string m_PluginListPath;
  loot::LogLevel m_LogLevel;
  bool m_UpdateMasterlist;
  mutable std::recursive_mutex mutex_;
  loot::GameSettings m_GameSettings;
  std::chrono::high_resolution_clock::time_point m_startTime;

  std::string createJsonReport(loot::GameInterface& game,
                               const std::vector<std::string>& sortedPlugins) const;

  QJsonArray createPlugins(loot::GameInterface& game,
                           const std::vector<std::string>& sortedPlugins) const;

  QJsonValue createMessages(const std::vector<loot::Message>& list) const;
  QJsonValue createDirty(const std::vector<loot::PluginCleaningData>& data) const;
  QJsonValue createClean(const std::vector<loot::PluginCleaningData>& data) const;

  QJsonValue createIncompatibilities(loot::GameInterface& game,
                                     const std::vector<loot::File>& data) const;

  QJsonValue createMissingMasters(loot::GameInterface& game,
                                  const std::string& pluginName) const;
};

}  // namespace lootcli

#endif  // LOOTTHREAD_H

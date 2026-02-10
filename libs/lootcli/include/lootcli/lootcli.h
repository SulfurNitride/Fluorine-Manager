#ifndef MODORGANIZER_LOOTCLI_INCLUDED
#define MODORGANIZER_LOOTCLI_INCLUDED

#include <regex>

namespace lootcli
{

enum class LogLevels
{
  Trace = 0,
  Debug,
  Info,
  Warning,
  Error
};

inline LogLevels logLevelFromString(const std::string& s)
{
  if (s == "trace") {
    return LogLevels::Trace;
  } else if (s == "debug") {
    return LogLevels::Debug;
  } else if (s == "info") {
    return LogLevels::Info;
  } else if (s == "warning") {
    return LogLevels::Warning;
  } else if (s == "error") {
    return LogLevels::Error;
  } else {
    return LogLevels::Info;
  }
}

inline std::string logLevelToString(LogLevels level)
{
  switch (level) {
  case LogLevels::Trace:
    return "trace";
  case LogLevels::Debug:
    return "debug";
  case LogLevels::Info:
    return "info";
  case LogLevels::Warning:
    return "warning";
  case LogLevels::Error:
    return "error";
  default:
    return "info";
  }
}

enum class Progress
{
  None = 0,
  CheckingMasterlistExistence,
  UpdatingMasterlist,
  LoadingLists,
  ReadingPlugins,
  SortingPlugins,
  WritingLoadorder,
  ParsingLootMessages,
  Done
};

enum class MessageType
{
  None = 0,
  Progress,
  Log
};

struct Message
{
  MessageType type   = MessageType::None;
  Progress progress  = Progress::None;
  LogLevels logLevel = LogLevels::Info;
  std::string log;

  static Message fromProgress(Progress p)
  {
    return {MessageType::Progress, p, LogLevels::Info, ""};
  }

  static Message fromLog(LogLevels level, std::string log)
  {
    return {MessageType::Log, Progress::None, level, std::move(log)};
  }
};

inline Message parseMessage(const std::string_view& line)
{
  static std::regex e(R"(^\[([a-z]+)\] (.+)$)");

  std::match_results<std::string_view::const_iterator> m;
  if (!std::regex_match(line.begin(), line.end(), m, e)) {
    return {};
  }

  const auto type = m[1];

  if (type == "progress") {
    try {
      const auto p = std::stoi(m[2]);
      return Message::fromProgress(static_cast<Progress>(p));
    } catch (std::exception&) {
      return {};
    }
  } else {
    return Message::fromLog(logLevelFromString(type), m[2]);
  }
}

}  // namespace lootcli

#endif  // MODORGANIZER_LOOTCLI_INCLUDED

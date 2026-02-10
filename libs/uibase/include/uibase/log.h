#pragma once

#include <QColor>
#include <QFlag>
#include <QFlags>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringView>
#include <filesystem>
#include <string>
#include <vector>

#include <format>

#include "dllimport.h"
#include "formatters.h"
#include <uibase/strings.h>

namespace spdlog
{
class logger;
}
namespace spdlog::sinks
{
class sink;
}

namespace MOBase::log
{

enum Levels
{
  Debug   = 0,
  Info    = 1,
  Warning = 2,
  Error   = 3
};

struct BlacklistEntry
{
  std::string filter;
  std::string replacement;
};

}  // namespace MOBase::log

namespace MOBase::log::details
{

// TODO: remove for C++23
template <typename T>
concept formattable = requires(T& v, std::format_context ctx) {
  std::formatter<std::remove_cvref_t<T>>().format(v, ctx);
};

template <typename F, typename... Args>
concept RuntimeFormatString = requires(F&& f, Args&&... args) {
  (formattable<Args> && ...);
  !std::is_convertible_v<std::decay_t<F>, std::string_view>;
};

void QDLLEXPORT doLogImpl(spdlog::logger& lg, Levels lv, const std::string& s) noexcept;

template <class... Args>
void doLog(spdlog::logger& logger, Levels lv,
           const std::vector<MOBase::log::BlacklistEntry> bl,
           std::format_string<Args...> format, Args&&... args) noexcept
{
  // format errors are logged without much information to avoid throwing again

  std::string s;
  try {
    s = std::format(format, std::forward<Args>(args)...);

    // check the blacklist
    for (const BlacklistEntry& entry : bl) {
      MOBase::ireplace_all(s, entry.filter, entry.replacement);
    }
  } catch (std::format_error&) {
    s  = "format error while logging";
    lv = Levels::Error;
  } catch (std::exception&) {
    s  = "exception while formatting for logging";
    lv = Levels::Error;
  } catch (...) {
    s  = "unknown exception while formatting for logging";
    lv = Levels::Error;
  }

  doLogImpl(logger, lv, s);
}

template <class F, class... Args>
void doLog(spdlog::logger& logger, Levels lv,
           const std::vector<MOBase::log::BlacklistEntry> bl, F&& format,
           Args&&... args) noexcept
{
  std::string s;

  // format errors are logged without much information to avoid throwing again

  try {
    if constexpr (sizeof...(Args) == 0) {
      s = std::format("{}", std::forward<F>(format));
    } else if constexpr (std::is_same_v<std::decay_t<F>, QString>) {
      s = std::vformat(format.toStdString(), std::make_format_args(args...));
    } else {
      s = std::vformat(std::forward<F>(format), std::make_format_args(args...));
    }

    // check the blacklist
    for (const BlacklistEntry& entry : bl) {
      MOBase::ireplace_all(s, entry.filter, entry.replacement);
    }
  } catch (std::format_error&) {
    s  = "format error while logging";
    lv = Levels::Error;
  } catch (std::exception&) {
    s  = "exception while formatting for logging";
    lv = Levels::Error;
  } catch (...) {
    s  = "unknown exception while formatting for logging";
    lv = Levels::Error;
  }

  doLogImpl(logger, lv, s);
}

}  // namespace MOBase::log::details

namespace MOBase::log
{

struct QDLLEXPORT File
{
public:
  enum Types
  {
    None = 0,
    Daily,
    Rotating,
    Single
  };

  File();

  static File daily(std::filesystem::path file, int hour, int minute);

  static File rotating(std::filesystem::path file, std::size_t maxSize,
                       std::size_t maxFiles);

  static File single(std::filesystem::path file);

  Types type;
  std::filesystem::path file;
  std::size_t maxSize, maxFiles;
  int dailyHour, dailyMinute;
};

struct Entry
{
  std::chrono::system_clock::time_point time;
  Levels level;
  std::string message;
  std::string formattedMessage;
};

using Callback = void(Entry);

struct LoggerConfiguration
{
  std::string name;
  Levels maxLevel = Levels::Info;
  std::string pattern;
  bool utc = false;
  std::vector<BlacklistEntry> blacklist;
};

class QDLLEXPORT Logger
{
public:
  Logger(LoggerConfiguration conf);
  ~Logger();

  Levels level() const;
  void setLevel(Levels lv);

  void setPattern(const std::string& pattern);
  void setFile(const File& f);
  void setCallback(Callback* f);

  void addToBlacklist(const std::string& filter, const std::string& replacement);
  void removeFromBlacklist(const std::string& filter);
  void resetBlacklist();

  template <class F, class... Args>
    requires(details::RuntimeFormatString<F, Args...>)
  void debug(F&& format, Args&&... args) noexcept
  {
    log(Debug, std::forward<F>(format), std::forward<Args>(args)...);
  }

  template <class... Args>
  void debug(std::format_string<Args...> format, Args&&... args) noexcept
  {
    log(Debug, format, std::forward<Args>(args)...);
  }

  template <class F, class... Args>
    requires(details::RuntimeFormatString<F, Args...>)
  void info(F&& format, Args&&... args) noexcept
  {
    log(Info, std::forward<F>(format), std::forward<Args>(args)...);
  }

  template <class... Args>
  void info(std::format_string<Args...> format, Args&&... args) noexcept
  {
    log(Info, format, std::forward<Args>(args)...);
  }

  template <class F, class... Args>
    requires(details::RuntimeFormatString<F, Args...>)
  void warn(F&& format, Args&&... args) noexcept
  {
    log(Warning, std::forward<F>(format), std::forward<Args>(args)...);
  }

  template <class... Args>
  void warn(std::format_string<Args...> format, Args&&... args) noexcept
  {
    log(Warning, format, std::forward<Args>(args)...);
  }

  template <class F, class... Args>
    requires(details::RuntimeFormatString<F, Args...>)
  void error(F&& format, Args&&... args) noexcept
  {
    log(Error, std::forward<F>(format), std::forward<Args>(args)...);
  }

  template <class... Args>
  void error(std::format_string<Args...> format, Args&&... args) noexcept
  {
    log(Error, format, std::forward<Args>(args)...);
  }

  template <class F, class... Args>
    requires(details::RuntimeFormatString<F, Args...>)
  void log(Levels lv, F&& format, Args&&... args) noexcept
  {
    details::doLog(*m_logger, lv, m_conf.blacklist, std::forward<F>(format),
                   std::forward<Args>(args)...);
  }

  template <class... Args>
  void log(Levels lv, std::format_string<Args...> format, Args&&... args) noexcept
  {
    details::doLog(*m_logger, lv, m_conf.blacklist, format,
                   std::forward<Args>(args)...);
  }

private:
  LoggerConfiguration m_conf;
  std::unique_ptr<spdlog::logger> m_logger;
  std::shared_ptr<spdlog::sinks::sink> m_sinks;
  std::shared_ptr<spdlog::sinks::sink> m_console, m_callback, m_file;

  void createLogger(const std::string& name);
  void addSink(std::shared_ptr<spdlog::sinks::sink> sink);
};

QDLLEXPORT void createDefault(LoggerConfiguration conf);
QDLLEXPORT Logger& getDefault();

template <class F, class... Args>
  requires(details::RuntimeFormatString<F, Args...>)
void debug(F&& format, Args&&... args) noexcept
{
  getDefault().debug(std::forward<F>(format), std::forward<Args>(args)...);
}

template <class... Args>
void debug(std::format_string<Args...> format, Args&&... args) noexcept
{
  getDefault().debug(format, std::forward<Args>(args)...);
}

template <class F, class... Args>
  requires(details::RuntimeFormatString<F, Args...>)
void info(F&& format, Args&&... args) noexcept
{
  getDefault().info(std::forward<F>(format), std::forward<Args>(args)...);
}

template <class... Args>
void info(std::format_string<Args...> format, Args&&... args) noexcept
{
  getDefault().info(format, std::forward<Args>(args)...);
}

template <class F, class... Args>
  requires(details::RuntimeFormatString<F, Args...>)
void warn(F&& format, Args&&... args) noexcept
{
  getDefault().warn(std::forward<F>(format), std::forward<Args>(args)...);
}

template <class... Args>
void warn(std::format_string<Args...> format, Args&&... args) noexcept
{
  getDefault().warn(format, std::forward<Args>(args)...);
}

template <class F, class... Args>
  requires(details::RuntimeFormatString<F, Args...>)
void error(F&& format, Args&&... args) noexcept
{
  getDefault().error(std::forward<F>(format), std::forward<Args>(args)...);
}

template <class... Args>
void error(std::format_string<Args...> format, Args&&... args) noexcept
{
  getDefault().error(format, std::forward<Args>(args)...);
}

template <class F, class... Args>
  requires(details::RuntimeFormatString<F, Args...>)
void log(Levels lv, F&& format, Args&&... args) noexcept
{
  getDefault().log(lv, std::forward<F>(format), std::forward<Args>(args)...);
}

template <class... Args>
void log(Levels lv, std::format_string<Args...> format, Args&&... args) noexcept
{
  getDefault().log(lv, format, std::forward<Args>(args)...);
}

//
QDLLEXPORT QString levelToString(Levels level);

}  // namespace MOBase::log

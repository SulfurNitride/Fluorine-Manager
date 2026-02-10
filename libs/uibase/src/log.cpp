#include <uibase/log.h>
#include "pch.h"
#include <uibase/utility.h>
#include <iostream>

#include <algorithm>
#include <locale>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4365)
#endif

#ifdef _WIN32
#define SPDLOG_WCHAR_FILENAMES 1
#endif

#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace MOBase::log
{

namespace fs = std::filesystem;
static std::unique_ptr<Logger> g_default;

spdlog::level::level_enum toSpdlog(Levels lv)
{
  switch (lv) {
  case Debug:
    return spdlog::level::debug;

  case Warning:
    return spdlog::level::warn;

  case Error:
    return spdlog::level::err;

  case Info:  // fall-through
  default:
    return spdlog::level::info;
  }
}

Levels fromSpdlog(spdlog::level::level_enum lv)
{
  switch (lv) {
  case spdlog::level::trace:
  case spdlog::level::debug:
    return Debug;

  case spdlog::level::warn:
    return Warning;

  case spdlog::level::critical:  // fall-through
  case spdlog::level::err:
    return Error;

  case spdlog::level::info:  // fall-through
  case spdlog::level::off:
  case spdlog::level::n_levels:  // to please MSVC
  default:
    return Info;
  }
}

class CallbackSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
  CallbackSink(Callback* f) : m_f(f) {}

  void setCallback(Callback* f) { m_f = f; }

protected:
  void sink_it_(const spdlog::details::log_msg& m) override
  {
    thread_local bool active = false;

    if (active) {
      // trying to log from a log callback, ignoring
      return;
    }

    if (!m_f) {
      // disabled
      return;
    }

    try {
      auto g = Guard([&] {
        active = false;
      });
      active = true;

      Entry e;
      e.time    = m.time;
      e.level   = fromSpdlog(m.level);
      e.message = std::string(m.payload);

      spdlog::memory_buf_t formatted;
      base_sink::formatter_->format(m, formatted);

      if (formatted.size() >= 2) {
        // remove \r\n
        e.formattedMessage.assign(formatted.begin(), formatted.end() - 2);
      } else {
        e.formattedMessage = std::string(formatted);
      }

      (*m_f)(std::move(e));
    } catch (std::exception& e) {
      fprintf(stderr, "uncaugh exception in logging callback, %s\n", e.what());
    } catch (...) {
      fprintf(stderr, "uncaught exception in logging callback\n");
    }
  }

  void flush_() override
  {
    // no-op
  }

private:
  std::atomic<Callback*> m_f;
};

File::File() : type(None), maxSize(0), maxFiles(0), dailyHour(0), dailyMinute(0) {}

File File::daily(fs::path file, int hour, int minute)
{
  File fl;

  fl.type        = Daily;
  fl.file        = std::move(file);
  fl.dailyHour   = hour;
  fl.dailyMinute = minute;

  return fl;
}

File File::rotating(fs::path file, std::size_t maxSize, std::size_t maxFiles)
{
  File fl;

  fl.type     = Rotating;
  fl.file     = std::move(file);
  fl.maxSize  = maxSize;
  fl.maxFiles = maxFiles;

  return fl;
}

File File::single(std::filesystem::path file)
{
  File fl;

  fl.type = Single;
  fl.file = std::move(file);

  return fl;
}

spdlog::sink_ptr createFileSink(const File& f)
{
  try {
    switch (f.type) {
    case File::Daily: {
      return std::make_shared<spdlog::sinks::daily_file_sink_mt>(
          f.file.native(), f.dailyHour, f.dailyMinute);
    }

    case File::Rotating: {
      return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          f.file.native(), f.maxSize, f.maxFiles);
    }

    case File::Single: {
      return std::make_shared<spdlog::sinks::basic_file_sink_mt>(f.file.native(), true);
    }

    case File::None:  // fall-through
    default:
      return {};
    }
  } catch (spdlog::spdlog_ex& e) {
    std::cerr << "failed to create file log, " << e.what() << "\n";
    return {};
  }
}

Logger::Logger(LoggerConfiguration conf_moved) : m_conf(std::move(conf_moved))
{
  createLogger(m_conf.name);

  const auto timeType =
      m_conf.utc ? spdlog::pattern_time_type::utc : spdlog::pattern_time_type::local;

  m_logger->set_level(toSpdlog(m_conf.maxLevel));
  m_logger->set_pattern(m_conf.pattern, timeType);
  m_logger->flush_on(spdlog::level::trace);
}

// anchor
Logger::~Logger() = default;

Levels Logger::level() const
{
  return fromSpdlog(m_logger->level());
}

void Logger::setLevel(Levels lv)
{
  m_logger->set_level(toSpdlog(lv));
}

void Logger::setPattern(const std::string& s)
{
  m_logger->set_pattern(s);
}

void Logger::setFile(const File& f)
{

  if (m_file) {
    auto* ds = static_cast<spdlog::sinks::dist_sink<std::mutex>*>(m_sinks.get());
    ds->remove_sink(m_file);
    m_file = {};
  }

  if (f.type != File::None) {
    try {
      m_file = createFileSink(f);

      if (m_file) {
        addSink(m_file);
      }
    } catch (spdlog::spdlog_ex& e) {
      error("{}", e.what());
    }
  }
}

void Logger::setCallback(Callback* f)
{
  if (m_callback) {
    static_cast<CallbackSink*>(m_callback.get())->setCallback(f);
  } else {
    m_callback.reset(new CallbackSink(f));
    addSink(m_callback);
  }
}

void Logger::addToBlacklist(const std::string& filter, const std::string& replacement)
{
  if (filter.length() <= 0 || replacement.length() <= 0) {
    // nothing to do
    return;
  }

  bool present = false;
  for (BlacklistEntry& e : m_conf.blacklist) {
    if (iequals(e.filter, filter)) {
      e.replacement = replacement;
      present       = true;
      break;
    }
  }
  if (!present) {
    m_conf.blacklist.push_back(BlacklistEntry(filter, replacement));
  }
}

void Logger::removeFromBlacklist(const std::string& filter)
{
  if (filter.length() <= 0) {
    // nothing to do
    return;
  }

  for (auto it = m_conf.blacklist.begin(); it != m_conf.blacklist.end();) {
    if (iequals(it->filter, filter)) {
      it = m_conf.blacklist.erase(it);
    } else {
      ++it;
    }
  }
}

void Logger::resetBlacklist()
{
  m_conf.blacklist.clear();
}

void Logger::createLogger(const std::string& name)
{
  m_sinks.reset(new spdlog::sinks::dist_sink<std::mutex>);

#ifdef _WIN32
  DWORD console_mode;
  if (::GetConsoleMode(::GetStdHandle(STD_ERROR_HANDLE), &console_mode) != 0) {
    using sink_type = spdlog::sinks::wincolor_stderr_sink_mt;
    m_console.reset(new sink_type);

    if (auto* cs = dynamic_cast<sink_type*>(m_console.get())) {
      cs->set_color(spdlog::level::info,
                    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
      cs->set_color(spdlog::level::debug,
                    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    addSink(m_console);
  }
#else
  // On Linux, use ANSI color stderr sink
  m_console.reset(new spdlog::sinks::ansicolor_stderr_sink_mt);
  addSink(m_console);
#endif

  m_logger.reset(new spdlog::logger(name, m_sinks));
}

void Logger::addSink(std::shared_ptr<spdlog::sinks::sink> sink)
{
  // this is called for both the file and callback sinks
  //
  // in createLogger(), the dist_sink that was just created will be given the
  // pattern that was set in Logger::Logger(), and will pass it to its children;
  // the log level is irrelevant in child sinks because dist_sink checks it
  // itself
  //
  // the problem then is that dist_sink doesn't have children yet, they're added
  // in setFile() and setCallback(), which can be called by the user much later
  // (or not at all)
  //
  // however, when a sink is added to dist_sink, it does _not_ set the pattern
  // on it, it merely adds it to the list
  //
  // this sets the formatter on the sink manually before adding it to dist_sink

  auto* ds = static_cast<spdlog::sinks::dist_sink<std::mutex>*>(m_sinks.get());

  const auto timeType =
      m_conf.utc ? spdlog::pattern_time_type::utc : spdlog::pattern_time_type::local;

  sink->set_formatter(
      std::make_unique<spdlog::pattern_formatter>(m_conf.pattern, timeType));

  ds->add_sink(sink);
}

QString levelToString(Levels level)
{
  const auto spdlogLevel = toSpdlog(level);
  const auto sv          = spdlog::level::to_string_view(spdlogLevel);
  const std::string s(sv.begin(), sv.end());

  return QString::fromStdString(s);
}

void createDefault(LoggerConfiguration conf)
{
  g_default = std::make_unique<Logger>(conf);
}

Logger& getDefault()
{
  Q_ASSERT(g_default);
  return *g_default;
}

}  // namespace MOBase::log

namespace MOBase::log::details
{

void doLogImpl(spdlog::logger& lg, Levels lv, const std::string& s) noexcept
{
  try {
    const char* start = s.c_str();
    const char* p     = start;

    for (;;) {
      while (*p && *p != '\n') {
        ++p;
      }

      std::string_view sv(start, static_cast<std::size_t>(p - start));
      lg.log(toSpdlog(lv), "{}", sv);

      if (!*p) {
        break;
      }

      ++p;
      start = p;
    }
  } catch (...) {
    // eat it
  }
}

}  // namespace MOBase::log::details

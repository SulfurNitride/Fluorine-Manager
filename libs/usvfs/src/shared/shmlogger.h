/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include "logging.h"
#include "shared_memory.h"
#include "windows_sane.h"

typedef boost::interprocess::message_queue_t<usvfs::shared::VoidPointerT>
    message_queue_interop;

namespace usvfs::sinks
{
class shm_sink : public spdlog::sinks::base_sink<spdlog::details::null_mutex>
{
public:
  shm_sink(const char* queueName);

protected:
  void sink_it_(const spdlog::details::log_msg& msg) override;
  void output(spdlog::level::level_enum lev, const std::string& message);
  void flush_() override;

private:
  message_queue_interop m_LogQueue;
  std::atomic<int> m_DroppedMessages;
};

}  // namespace usvfs::sinks

class SHMLogger
{
public:
  static const size_t MESSAGE_COUNT = 1024;
  static const size_t MESSAGE_SIZE  = 512;

  static SHMLogger& create(const char* instanceName);
  static SHMLogger& open(const char* instanceName);
  static void free();

  static bool isInstantiated() { return s_Instance != nullptr; }

  static inline SHMLogger& instance()
  {
    if (s_Instance == nullptr) {
      throw std::runtime_error("shm logger not instantiated");
    }

    return *s_Instance;
  }

  void log(LogLevel logLevel, const std::string& message);
  bool tryGet(char* buffer, size_t bufferSize);
  void get(char* buffer, size_t bufferSize);

private:
  struct owner_t
  {};
  static owner_t owner;

  struct client_t
  {};
  static client_t client;

private:
  SHMLogger(owner_t, const std::string& instanceName);
  SHMLogger(client_t, const std::string& instanceName);

  SHMLogger(const SHMLogger&)            = delete;
  SHMLogger& operator=(const SHMLogger&) = delete;

  ~SHMLogger();

private:
  static SHMLogger* s_Instance;

  message_queue_interop m_LogQueue;

  std::string m_SHMName;
  std::string m_LockName;
  std::string m_QueueName;

  std::atomic<int> m_DroppedMessages;
};

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
#pragma warning(disable : 4714)
#pragma warning(disable : 4503)
#pragma warning(push, 3)
#include "shmlogger.h"
#pragma warning(pop)

#pragma warning(disable : 4996)

using namespace boost::interprocess;
using namespace boost::posix_time;

SHMLogger* SHMLogger::s_Instance = nullptr;

SHMLogger::owner_t SHMLogger::owner;
SHMLogger::client_t SHMLogger::client;

SHMLogger::SHMLogger(owner_t, const std::string& queueName)
    : m_QueueName(queueName),
      m_LogQueue(create_only, queueName.c_str(), MESSAGE_COUNT, MESSAGE_SIZE),
      m_DroppedMessages(0)
{
  if (s_Instance != nullptr) {
    throw std::runtime_error("duplicate shm logger instantiation");
  } else {
    s_Instance = this;
  }
}

SHMLogger::SHMLogger(client_t, const std::string& queueName)
    : m_QueueName(queueName), m_LogQueue(open_only, queueName.c_str()),
      m_DroppedMessages(0)
{
  if (s_Instance != nullptr) {
    throw std::runtime_error("duplicate shm logger instantiation");
  } else {
    s_Instance = this;
  }
}

SHMLogger::~SHMLogger()
{
  s_Instance = nullptr;
  message_queue_interop::remove(m_QueueName.c_str());
}

SHMLogger& SHMLogger::create(const char* instanceName)
{
  if (s_Instance != nullptr) {
    throw std::runtime_error("duplicate shm logger instantiation");
  } else {
    std::string queueName = std::string("__shm_sink_") + instanceName;
    message_queue::remove(queueName.c_str());
    new SHMLogger(owner, queueName);
    atexit([]() {
      delete s_Instance;
    });
  }
  return *s_Instance;
}

SHMLogger& SHMLogger::open(const char* instanceName)
{
  if (s_Instance != nullptr) {
    throw std::runtime_error("duplicate shm logger instantiation");
  } else {
    new SHMLogger(client, std::string("__shm_sink_") + instanceName);
  }
  return *s_Instance;
}

void SHMLogger::free()
{
  if (s_Instance != nullptr) {
    SHMLogger* temp = s_Instance;
    s_Instance      = nullptr;
    delete temp;
  }
}

bool SHMLogger::tryGet(char* buffer, size_t bufferSize)
{
  message_queue_interop::size_type receivedSize;
  unsigned int prio;
  bool res = m_LogQueue.try_receive(buffer, static_cast<unsigned int>(bufferSize),
                                    receivedSize, prio);
  if (res) {
    buffer[std::min(bufferSize - 1, static_cast<size_t>(receivedSize))] = '\0';
  }
  return res;
}

void SHMLogger::get(char* buffer, size_t bufferSize)
{
  message_queue_interop::size_type receivedSize;
  unsigned int prio;
  m_LogQueue.receive(buffer, static_cast<unsigned int>(bufferSize), receivedSize, prio);
  buffer[std::min(bufferSize - 1, static_cast<size_t>(receivedSize))] = '\0';
}

usvfs::sinks::shm_sink::shm_sink(const char* queueName)
    : m_LogQueue(open_only, (std::string("__shm_sink_") + queueName).c_str()),
      m_DroppedMessages(0)
{}

void usvfs::sinks::shm_sink::flush_() {}

void usvfs::sinks::shm_sink::sink_it_(const spdlog::details::log_msg& msg)
{
  int droppedMessages = m_DroppedMessages.load(std::memory_order_relaxed);
  if (droppedMessages > 0) {
    auto dropMessage = std::format("{} debug messages dropped", droppedMessages);
    if (m_LogQueue.try_send(dropMessage.c_str(),
                            static_cast<unsigned int>(dropMessage.size()), 0)) {
      m_DroppedMessages.store(0, std::memory_order_relaxed);
    }
  }

  std::string message{msg.payload};

  // blacklist %USERNAME% because PII
  static const std::string username = std::string(getenv("USERNAME"));
  boost::algorithm::ireplace_all(message, std::string("\\") + username, "\\USERNAME");
  boost::algorithm::ireplace_all(message, std::string("/") + username, "/USERNAME");

  if (message.length() > SHMLogger::MESSAGE_SIZE) {
    std::vector<std::string> splitVec;
    boost::split(splitVec, message, boost::is_any_of("\r\n"), boost::token_compress_on);
    for (const std::string& line : splitVec) {
      output(msg.level, line);
    }
  } else {
    output(msg.level, message);
  }
}

void usvfs::sinks::shm_sink::output(spdlog::level::level_enum lev,
                                    const std::string& message)
{
  bool sent = true;

  // spdlog auto-append line breaks which we don't need. Probably would be
  // better to not write the breaks to begin with?
  size_t count = std::min(message.find_last_not_of("\r\n") + 1,
                          static_cast<size_t>(m_LogQueue.get_max_msg_size()));

  // depending on the log level, drop less important messages if the receiver
  // can't keep up
  switch (lev) {
  case spdlog::level::trace:
  case spdlog::level::debug:
  case spdlog::level::info: {
    // m_LogQueue.send(message.c_str(), count, 0);
    sent = m_LogQueue.try_send(message.c_str(), static_cast<unsigned int>(count), 0);
  } break;
  case spdlog::level::err:
  case spdlog::level::critical: {
    m_LogQueue.send(message.c_str(), static_cast<unsigned int>(count), 0);
  } break;
  default: {
    boost::posix_time::ptime time =
        microsec_clock::universal_time() + boost::posix_time::milliseconds(200);
    sent = m_LogQueue.timed_send(message.c_str(), static_cast<unsigned int>(count), 0,
                                 time);
  } break;
  }

  if (!sent) {
    m_DroppedMessages.fetch_add(1, std::memory_order_relaxed);
  }
}

void __cdecl boost::interprocess::ipcdetail::get_shared_dir(std::string& shared_dir)
{
  PWSTR path;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, NULL, &path))) {
    _bstr_t bPath(path);
    shared_dir = (char*)bPath;
    shared_dir += "\\USVFS";
  } else {
    shared_dir = "C:\\ProgramData\\USVFS";
  }
  boost::filesystem::path boostPath(shared_dir);
  if (!boost::filesystem::exists(boostPath))
    boost::filesystem::create_directories(boostPath);
}

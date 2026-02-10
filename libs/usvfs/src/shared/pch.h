#pragma once

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cassert>
#include <codecvt>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <future>
#include <iomanip>
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <comutil.h>
#include <Psapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <fileapi.h>
#include <DbgHelp.h>
// clang-format on

#define BOOST_INTERPROCESS_SEGMENT_MANAGER_ABI 1
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/any.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/container/map.hpp>
#include <boost/container/scoped_allocator.hpp>
#include <boost/container/slist.hpp>
#include <boost/container/string.hpp>
#include <boost/container/vector.hpp>
#include <boost/current_function.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/smart_ptr/deleter.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/smart_ptr/weak_ptr.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/predef.h>
#include <boost/static_assert.hpp>
#include <boost/thread.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/tokenizer.hpp>
#include <boost/type_traits.hpp>

#include <spdlog/spdlog.h>

namespace fs = boost::filesystem;

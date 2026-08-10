#ifndef APPLICATIONRUNNERREGISTRY_H
#define APPLICATIONRUNNERREGISTRY_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <sys/types.h>

// Capacity bounds terminal, cleanup-finished entries only. Running and
// cleanup-pending entries are never evicted, even when they make the total
// registry size exceed Capacity.
template <class Payload, std::size_t Capacity = 64>
class ApplicationRunnerRegistry
{
public:
  static constexpr std::size_t CompletedCapacity = Capacity;

  struct Entry
  {
    pid_t pid;
    Payload payload;
  };

  static constexpr std::uintptr_t HandleTag =
      std::uintptr_t{1} << (sizeof(std::uintptr_t) * 8 - 1);
  static constexpr std::uintptr_t HandleValueMask = HandleTag - 1;

  static bool isOpaqueHandle(std::uintptr_t handle)
  {
    return (handle & HandleTag) != 0 &&
           handle != std::numeric_limits<std::uintptr_t>::max();
  }

  std::uintptr_t insert(pid_t pid, Payload payload)
  {
    std::lock_guard lock(m_Mutex);
    std::uintptr_t handle = 0;
    do {
      const std::uintptr_t value = m_NextValue++ & HandleValueMask;
      if (value != 0 && value != HandleValueMask) {
        handle = HandleTag | value;
      }
    } while (handle == 0 || m_Entries.contains(handle));

    m_Entries.emplace(handle, Entry{pid, std::move(payload)});
    try {
      m_Order.push_back(handle);
    } catch (...) {
      m_Entries.erase(handle);
      throw;
    }
    return handle;
  }

  std::optional<Entry> take(std::uintptr_t handle)
  {
    std::lock_guard lock(m_Mutex);
    const auto stored = m_Entries.find(handle);
    if (stored == m_Entries.end()) {
      return std::nullopt;
    }

    Entry result = std::move(stored->second);
    m_Entries.erase(stored);
    std::erase(m_Order, handle);
    return result;
  }

  bool release(std::uintptr_t handle)
  {
    std::lock_guard lock(m_Mutex);
    const auto stored = m_Entries.find(handle);
    if (stored == m_Entries.end()) {
      return false;
    }
    m_Entries.erase(stored);
    std::erase(m_Order, handle);
    return true;
  }

  // Expired completed entries are removed first. Under capacity pressure the
  // oldest otherwise-retirable entries are then removed, but active/pending
  // entries are never invalidated even if that temporarily exceeds Capacity.
  template <class CanRetire, class IsExpired>
  std::vector<std::uintptr_t> prune(CanRetire canRetire, IsExpired isExpired)
  {
    std::lock_guard lock(m_Mutex);
    std::vector<std::uintptr_t> retired;

    auto eraseIf = [&](auto predicate) {
      for (auto order = m_Order.begin(); order != m_Order.end();) {
        const auto entry = m_Entries.find(*order);
        if (entry == m_Entries.end()) {
          order = m_Order.erase(order);
        } else if (predicate(entry->second)) {
          retired.push_back(*order);
          m_Entries.erase(entry);
          order = m_Order.erase(order);
        } else {
          ++order;
        }
      }
    };

    eraseIf(isExpired);
    std::size_t retirableCount = static_cast<std::size_t>(std::count_if(
        m_Entries.cbegin(), m_Entries.cend(), [&](const auto& item) {
          return canRetire(item.second);
        }));
    while (retirableCount > Capacity) {
      const auto candidate = std::find_if(
          m_Order.begin(), m_Order.end(), [&](std::uintptr_t handle) {
            const auto entry = m_Entries.find(handle);
            return entry != m_Entries.end() && canRetire(entry->second);
          });
      if (candidate == m_Order.end()) {
        break;
      }
      retired.push_back(*candidate);
      m_Entries.erase(*candidate);
      m_Order.erase(candidate);
      --retirableCount;
    }
    return retired;
  }

  std::size_t size() const
  {
    std::lock_guard lock(m_Mutex);
    return m_Entries.size();
  }

private:
  mutable std::mutex m_Mutex;
  std::map<std::uintptr_t, Entry> m_Entries;
  std::deque<std::uintptr_t> m_Order;
  std::uintptr_t m_NextValue = 1;
};

#endif  // APPLICATIONRUNNERREGISTRY_H

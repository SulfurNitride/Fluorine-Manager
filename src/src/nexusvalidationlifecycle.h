#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace nexus_validation
{

template <typename Attempt>
auto snapshotOutcome(const Attempt& attempt)
{
  using Result  = std::remove_cvref_t<decltype(attempt.result())>;
  using Message = std::remove_cvref_t<decltype(attempt.message())>;
  return std::pair<Result, Message>{attempt.result(), attempt.message()};
}

template <typename Callback, typename... Args>
void invokeTerminalCallback(Callback& slot, Args&&... args)
{
  // The callback is allowed to destroy the object that owns the slot. Keep the
  // active callable off that object and do not touch the slot after invocation.
  auto callback = std::exchange(slot, Callback{});
  if (callback) {
    callback(std::forward<Args>(args)...);
  }
}

}  // namespace nexus_validation

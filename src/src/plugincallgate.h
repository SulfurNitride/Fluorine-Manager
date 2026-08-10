#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include "settingswritebarrier.h"

// Shared by queued host callbacks and OrganizerProxy helper objects. Closing
// the gate makes a stale callback reject before dereferencing its raw plugin
// pointer, while the process-wide barrier suppresses all plugin calls during
// terminal fail-stop.
class PluginCallGate
{
public:
  explicit PluginCallGate(
      std::shared_ptr<SettingsWriteBarrier> mutationBarrier)
      : m_MutationBarrier(std::move(mutationBarrier))
  {}

  template <typename Call>
  bool runIfAllowed(Call&& call) const
  {
    if (m_Closed.load(std::memory_order_acquire)) {
      return false;
    }
    return m_MutationBarrier->runIfAllowed(std::forward<Call>(call));
  }

  void close() noexcept
  {
    m_Closed.store(true, std::memory_order_release);
  }

private:
  std::atomic_bool m_Closed{false};
  std::shared_ptr<SettingsWriteBarrier> m_MutationBarrier;
};

enum class PluginReloadDecision
{
  Load,
  RestartRequired
};

inline PluginReloadDecision pluginReloadDecision(bool generationLoaded) noexcept
{
  return generationLoaded ? PluginReloadDecision::RestartRequired
                          : PluginReloadDecision::Load;
}

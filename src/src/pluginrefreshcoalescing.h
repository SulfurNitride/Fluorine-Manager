#pragma once

#include <cstdint>

class PluginRefreshCoalescing
{
public:
  using Serial = std::uint64_t;

  [[nodiscard]] Serial snapshot() const noexcept { return m_Serial; }

  [[nodiscard]] Serial begin(bool forced) noexcept
  {
    ++m_Serial;
    m_Forced    = forced;
    m_Succeeded = false;
    return m_Serial;
  }

  void complete(Serial attempt, bool succeeded) noexcept
  {
    if (attempt == m_Serial) {
      m_Succeeded = succeeded;
    }
  }

  [[nodiscard]] bool canSkipFallbackSince(Serial before) const noexcept
  {
    return m_Serial != before && m_Forced && m_Succeeded;
  }

private:
  Serial m_Serial{0};
  bool m_Forced{false};
  bool m_Succeeded{false};
};

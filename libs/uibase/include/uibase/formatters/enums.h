#pragma once

#include <format>
#include <type_traits>

template <class Enum, class CharT>
  requires std::is_enum_v<Enum>
struct std::formatter<Enum, CharT> : std::formatter<std::underlying_type_t<Enum>, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(Enum v, FmtContext& ctx) const
  {
    return std::formatter<std::underlying_type_t<Enum>, CharT>::format(
        static_cast<std::underlying_type_t<Enum>>(v), ctx);
  }
};

#pragma once

#include <string>
#include <string_view>

#include "dllimport.h"

namespace MOBase
{
#ifdef __cplusplus
extern "C++" {
#endif

QDLLEXPORT void ireplace_all(std::string& input, std::string_view search,
                             std::string_view replace) noexcept;

QDLLEXPORT bool iequals(std::string_view lhs, std::string_view rhs);

#ifdef __cplusplus
}
#endif
}  // namespace MOBase

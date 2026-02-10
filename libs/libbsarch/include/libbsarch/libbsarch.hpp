#pragma once

#include "base_types.hpp"
#include "utils/convertible_ostream.hpp"
#include "utils/convertible_string.hpp"

#include <cstring>

namespace libbsarch {
constexpr bool enable_debug_log = true;
[[maybe_unused]] constexpr int max_string_buffer_size = 1024;

inline convertible_ostream &debug_log()
{
    static convertible_ostream ostr;
    if constexpr (enable_debug_log)
        return ostr << "[libbsarch] " << __FUNCTION__ << ' ';
}

inline void checkResult(const bsa_result_message_s &result)
{
    if (result.code == BSA_RESULT_EXCEPTION)
    {
        // Copy from packed struct to aligned local buffer to avoid UB
        wchar_t aligned_text[1024];
        std::memcpy(aligned_text, result.text, sizeof(aligned_text));
        const std::string error = to_string(std::wstring(aligned_text));
        debug_log() << error;
        throw std::runtime_error(error);
    }
}

inline void checkResult(const bsa_result_message_buffer_s &result)
{
    checkResult(result.message);
}
} // namespace libbsarch

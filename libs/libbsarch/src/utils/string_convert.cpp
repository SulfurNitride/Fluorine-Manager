/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "string_convert.hpp"

#include <cstdlib>
#include <cstring>

namespace libbsarch {

std::string to_string(const std::wstring &str)
{
    if (str.empty()) return {};
    std::mbstate_t state{};
    const wchar_t* src = str.data();
    std::size_t len = std::wcsrtombs(nullptr, &src, 0, &state);
    if (len == static_cast<std::size_t>(-1)) return {};
    std::string result(len, '\0');
    src = str.data();
    state = std::mbstate_t{};
    std::wcsrtombs(result.data(), &src, len + 1, &state);
    return result;
}

std::wstring to_wstring(const std::string &str)
{
    if (str.empty()) return {};
    std::mbstate_t state{};
    const char* src = str.data();
    std::size_t len = std::mbsrtowcs(nullptr, &src, 0, &state);
    if (len == static_cast<std::size_t>(-1)) return {};
    std::wstring result(len, L'\0');
    src = str.data();
    state = std::mbstate_t{};
    std::mbsrtowcs(result.data(), &src, len + 1, &state);
    return result;
}
} // namespace libbsarch

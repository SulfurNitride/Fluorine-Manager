/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "convertible_string.hpp"
#include <cwchar>
#include <filesystem>

namespace libbsarch {
/* conversion ctors */
convertible_string::convertible_string(std::string value, bool to_native_path)
    : str_(std::move(value))
    , auto_convert_to_native_path(to_native_path)
{
    if (auto_convert_to_native_path)
        this->to_native_path();
}

convertible_string::convertible_string(const char *const val_array, bool to_native_path)
    : str_(std::string(val_array))
    , auto_convert_to_native_path(to_native_path)

{
    if (auto_convert_to_native_path)
        this->to_native_path();
}
convertible_string::convertible_string(const std::wstring &wvalue, bool to_native_path)
    : str_(to_string(wvalue))
    , auto_convert_to_native_path(to_native_path)

{
    if (auto_convert_to_native_path)
        this->to_native_path();
}
convertible_string::convertible_string(wchar_t const *wval_array, bool to_native_path)
    : str_(to_string(std::wstring(wval_array)))
    , auto_convert_to_native_path(to_native_path)
{
    if (auto_convert_to_native_path)
        this->to_native_path();
}

/* assignment operators */
convertible_string &convertible_string::operator=(const std::string &value)
{
    str_ = value;
    if (auto_convert_to_native_path)
        this->to_native_path();
    return *this;
}
convertible_string &convertible_string::operator=(const std::wstring &wvalue)
{
    str_ = to_string(wvalue);
    if (auto_convert_to_native_path)
        this->to_native_path();
    return *this;
}

convertible_string &convertible_string::operator=(const wchar_t *wvalue)
{
    str_ = to_string(std::wstring(wvalue));
    if (auto_convert_to_native_path)
        this->to_native_path();
    return *this;
}

/* implicit conversion operators */
convertible_string::operator std::string() const
{
    return str_;
}
convertible_string::operator std::wstring() const
{
    return to_wstring(str_);
}
convertible_string::operator const wchar_t *() const
{
    const std::wstring wstr = to_wstring(str_);
    wchar_t *wchar_array = new wchar_t[wstr.length() + 1];
#ifdef _WIN32
    wcscpy_s(wchar_array, wstr.length() + 1, wstr.c_str());
#else
    std::wmemcpy(wchar_array, wstr.c_str(), wstr.length() + 1);
#endif
    return wchar_array;
}

/* Util */
bool convertible_string::remove_substring(const convertible_string &sub_str)
{
    size_t pos = str_.find(sub_str);
    if (pos != std::string::npos)
    {
        str_.erase(pos, std::string(sub_str).length());
        return true;
    }
    return false;
}

convertible_string &convertible_string::to_native_path()
{
    std::filesystem::path path(str_);
    str_ = path.make_preferred().string();
    return *this;
}

} // namespace libbsarch

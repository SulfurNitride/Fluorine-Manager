/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#pragma once

#ifdef LIBBSARCH_QT_SUPPORT
#include <QDebug>
#endif

#include <codecvt>
#include <locale>
#include <string>

namespace libbsarch {
std::string to_string(const std::wstring &str);
std::wstring to_wstring(const std::string &str);
} // namespace libbsarch

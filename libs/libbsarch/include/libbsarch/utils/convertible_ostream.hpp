/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#pragma once

#include "convertible_string.hpp"
#include <iostream>

namespace libbsarch {
class convertible_ostream
{
public:
    convertible_ostream &operator<<(const std::string &rh);
    convertible_ostream &operator<<(const char *rh);
    convertible_ostream &operator<<(const char rh);
    convertible_ostream &operator<<(const std::wstring &rh);
    convertible_ostream &operator<<(const void *rh);
    convertible_ostream &operator<<(const convertible_string &rh);

    template<typename number, //real type
             typename = typename std::enable_if<std::is_arithmetic<number>::value, number>::type>
    convertible_ostream &operator<<(const number &num)
    {
        std::cout << num;
        return *this;
    }

    operator std::ostream &();
    operator std::wostream &();

#ifdef LIBBSARCH_QT_SUPPORT
    operator QDebug();
    convertible_ostream &operator<<(const QString &rh);
#endif
};
} // namespace libbsarch

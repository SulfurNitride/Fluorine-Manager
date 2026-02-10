/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "convertible_ostream.hpp"

namespace libbsarch {
convertible_ostream &convertible_ostream::operator<<(const std::string &rh)
{
    std::cout << rh;
    return *this;
}
convertible_ostream &convertible_ostream::operator<<(const char *rh)
{
    std::cout << rh;
    return *this;
}
convertible_ostream &convertible_ostream::operator<<(const char rh)
{
    std::cout << rh;
    return *this;
}
convertible_ostream &convertible_ostream::operator<<(const std::wstring &rh)
{
    std::wcout << rh;
    return *this;
}
convertible_ostream &convertible_ostream::operator<<(const void *rh)
{
    std::cout << rh;
    return *this;
}
convertible_ostream &convertible_ostream::operator<<(const convertible_string &rh)
{
    return *this << std::string(rh);
}

convertible_ostream::operator std::ostream &()
{
    return std::cout;
}
convertible_ostream::operator std::wostream &()
{
    return std::wcout;
}

#ifdef LIBBSARCH_QT_SUPPORT
convertible_ostream::operator QDebug()
{
    return qDebug();
}
convertible_ostream &convertible_ostream::operator<<(const QString &rh)
{
    qDebug() << rh;
    return *this;
}
#endif
} // namespace libbsarch

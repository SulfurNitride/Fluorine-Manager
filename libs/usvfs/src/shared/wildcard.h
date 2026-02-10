/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
/// Wildcard matching code
/// by Martin Richter
/// licensed under The Code Project Open License (CPOL)

#pragma once

#include "windows_sane.h"

namespace usvfs::shared::wildcard
{

/**
 * @brief match string to wildcard windows-style
 * @param pszString Input string to match
 * @param pszMatch Match mask that may contain wildcards like ? and *
 * @note A ? sign matches any character, except an empty string.
 * @note A * sign matches any string inclusive an empty string.
 * @note Characters are compared caseless.
 * @return true if the string matches the pattern
 */
bool Match(LPCWSTR pszString, LPCWSTR pszMatch);

/**
 * @brief match string to wildcard windows-style
 * @param pszString Input string to match
 * @param pszMatch Match mask that may contain wildcards like ? and *
 * @note A ? sign matches any character, except an empty string.
 * @note A * sign matches any string inclusive an empty string.
 * @note Characters are compared caseless.
 * @return true if the string matches the pattern
 */
bool Match(LPCSTR pszString, LPCSTR pszMatch);

/**
 * @brief match string to wildcard windows-style
 * @param pszString Input string to match
 * @param pszMatch Match mask that may contain wildcards like ? and *
 * @note A ? sign matches any character, except an empty string.
 * @note A * sign matches any string inclusive an empty string.
 * @note Characters are compared caseless.
 * @return the "not-consumed" remainder of the pattern. If this points to a
 *         zero terminator, this was a full match.
 *         Returns nullptr if no match is possible
 */
LPCSTR PartialMatch(LPCSTR pszString, LPCSTR pszMatch);

}  // namespace usvfs::shared::wildcard

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
#include "wildcard.h"
#include "logging.h"
#include "windows_sane.h"

static bool IsInnerMatch(LPCWSTR pszString, LPCWSTR pszMatch)
{
  while (*pszMatch != L'\0') {
    if ((*pszMatch == L'?') || (*pszMatch == L'>')) {
      if (!*pszString) {
        // ? must match exactly one character
        return false;
      }

      ++pszString;
      ++pszMatch;
    } else if ((*pszMatch == L'*') || (*pszMatch == L'<')) {
      if (IsInnerMatch(pszString, pszMatch + 1)) {
        // * may match empty string or we may have matched something already
        return true;
      }

      return *pszString && IsInnerMatch(pszString + 1, pszMatch);

      // the rest of the string can't be matched
    } else {
      if (CharUpperW(MAKEINTRESOURCEW(MAKELONG(*pszString++, 0))) !=
          CharUpperW(MAKEINTRESOURCEW(MAKELONG(*pszMatch++, 0)))) {
        // regular chars compare
        return false;
      }
    }
  }

  return !*pszString && !*pszMatch;
}

static LPCSTR InnerMatch(LPCSTR pszString, LPCSTR pszMatch)
{
  // We have a special case where string is empty ("") and the mask is "*".
  // We need to handle this too. So we can't test on !*pszString here.
  // The loop breaks when the match string is exhausted.
  while (*pszString != '\0') {
    // Single wildcard character
    if ((*pszMatch == '?') || (*pszMatch == '>')) {
      // can't match directory separator
      if ((*pszString == '\\') || (*pszString == '/'))
        return nullptr;

      // Matches any character except empty string
      if (*pszString == '\0') {
        // string consumed, part of the pattern is left
        return pszMatch;
      }

      // OK next
      ++pszString;
      ++pszMatch;
    } else if ((*pszMatch == '*') || (*pszMatch == '<')) {
      // * doesn't match directory separators
      if ((*pszString == '\\') || (*pszString == '/')) {
        ++pszMatch;
        continue;
      }

      // Need to do some tricks.

      // 1. The wildcard * is ignored.
      //    So just an empty string matches. This is done by recursion.
      //      Because we eat one character from the match string, the
      //      recursion will stop.
      {
        LPCSTR remainder = InnerMatch(pszString, pszMatch + 1);
        if (remainder != nullptr) {
          // we have a match and the * replaces no other character
          return remainder;
        }
      }

      // 2. Chance we eat the next character and try it again, with a
      //    wildcard * match. This is done by recursion. Because we eat
      //      one character from the string, the recursion will stop.
      if (*pszString != '\0') {
        LPCSTR remainder = InnerMatch(pszString + 1, pszMatch);
        if (remainder != nullptr) {
          return remainder;
        }
      }

      // Nothing worked with this wildcard.
      return nullptr;
    } else {
      // Standard compare of 2 chars. Note that *pszSring might be 0
      // here, but then we never get a match on *pszMask that has always
      // a value while inside this loop.
      if (CharUpperA(MAKEINTRESOURCEA(MAKELONG(*pszString++, 0))) !=
          CharUpperA(MAKEINTRESOURCEA(MAKELONG(*pszMatch++, 0))))
        return nullptr;
    }
  }

  // successful match if the input string was completely consumed
  if (*pszString == '\0') {
    while ((*pszMatch == '*') || (*pszMatch == '<')) {
      ++pszMatch;
    }
    return pszMatch;
  } else {
    return nullptr;
  }
}

namespace usvfs::shared::wildcard
{

bool Match(LPCWSTR pszString, LPCWSTR pszMatch)
{
  if (*pszString == L'.') {
    // cmd.exe seems to ignore
    return Match(pszString + 1, pszMatch);
  } else {
    size_t len = wcslen(pszMatch);
    if ((len > 2) && (wcscmp(pszMatch + len - 2, L".*") == 0)) {
      // cmd.exe seems to completely ignore .* at the end.
      std::wstring temp(pszMatch, pszMatch + len - 2);
      return IsInnerMatch(pszString, temp.c_str());
    }
    return IsInnerMatch(pszString, pszMatch);
  }
}

bool Match(LPCSTR pszString, LPCSTR pszMatch)
{
  if (*pszString == '.') {
    // cmd.exe seems to ignore
    return Match(pszString + 1, pszMatch);
  } else {
    size_t len = strlen(pszMatch);
    LPCSTR res = nullptr;
    if ((len > 2) && (strcmp(pszMatch + len - 2, ".*") == 0)) {
      // cmd.exe seems to completely ignore .* at the end.
      std::string temp(pszMatch, pszMatch + len - 2);
      res = InnerMatch(pszString, temp.c_str());
    } else {
      res = InnerMatch(pszString, pszMatch);
    }
    return ((res != nullptr) && (*res == '\0'));
  }
}

LPCSTR PartialMatch(LPCSTR pszString, LPCSTR pszMatch)
{
  if (*pszString == '.') {
    // cmd.exe seems to ignore dots at the start
    return PartialMatch(pszString + 1, pszMatch);
  } else {
    size_t len = strlen(pszMatch);
    if ((len > 2) && (strcmp(pszMatch + len - 2, ".*") == 0)) {
      // in cmd.exe there seems to be no difference between <something>* and
      // <something>*.*
      std::string temp(pszMatch, pszMatch + len - 2);
      LPCSTR pos = InnerMatch(pszString, temp.c_str());
      if (pos != nullptr) {
        if (*pos == '\0') {
          return pszMatch + strlen(pszMatch);
        } else {
          return pszMatch + (pos - temp.c_str());
        }
      }
    }
    return InnerMatch(pszString, pszMatch);
  }
}

}  // namespace usvfs::shared::wildcard

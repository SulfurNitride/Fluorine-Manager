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
#include "stringutils.h"
#include "windows_sane.h"

namespace usvfs::shared
{

void strncpy_sz(char* dest, const char* src, size_t destSize)
{
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
  }
}

void wcsncpy_sz(wchar_t* dest, const wchar_t* src, size_t destSize)
{
  if ((destSize > 0) && (dest != nullptr)) {
    wcsncpy(dest, src, destSize - 1);
    dest[destSize - 1] = L'\0';
  }
}

bool startswith(const wchar_t* string, const wchar_t* subString)
{
  while ((*string != '\0') && (*subString != '\0')) {
    if (towlower(*string) != towlower(*subString)) {
      return false;
    }
    ++string;
    ++subString;
  }

  return *subString == '\0';
}

static fs::path normalize(const fs::path& path)
{
  fs::path result;

  boost::locale::generator gen;
  auto loc = gen("en_US.UTF-8");
  for (fs::path::iterator iter = path.begin(); iter != path.end(); ++iter) {
    if (*iter == "..") {
      result = result.parent_path();
    } else if (*iter != ".") {
      result /= boost::to_lower_copy(iter->string(), loc);
    }  // single dot is ignored
  }
  return result;
}

fs::path make_relative(const fs::path& fromIn, const fs::path& toIn)
{
  // converting path to lower case to make iterator comparison work correctly
  // on case-insenstive filesystems
  fs::path from(fs::absolute(fromIn));
  fs::path to(fs::absolute(toIn));

  // find common base
  fs::path::const_iterator fromIter(from.begin());
  fs::path::const_iterator toIter(to.begin());

  // TODO the following equivalent test is probably quite expensive as new
  // paths are created for each iteration but the case sensitivity depends on
  // the fs
  while ((fromIter != from.end()) && (toIter != to.end()) &&
         (boost::iequals(fromIter->string(), toIter->string()))) {
    ++fromIter;
    ++toIter;
  }

  // Navigate backwards in directory to reach previously found base
  fs::path result;
  for (; fromIter != from.end(); ++fromIter) {
    if (*fromIter != ".") {
      result /= "..";
    }
  }

  // Now navigate down the directory branch
  for (; toIter != to.end(); ++toIter) {
    result /= *toIter;
  }
  return result;
}

std::string to_hex(void* bufferIn, size_t bufferSize)
{
  unsigned char* buffer = static_cast<unsigned char*>(bufferIn);
  std::ostringstream temp;
  temp << std::hex;
  for (size_t i = 0; i < bufferSize; ++i) {
    temp << std::setfill('0') << std::setw(2) << (unsigned int)buffer[i];
    if ((i % 16) == 15) {
      temp << "\n";
    } else {
      temp << " ";
    }
  }
  return temp.str();
}

std::wstring to_upper(const std::wstring& input)
{
  std::wstring result;
  result.resize(input.size());
  ::LCMapStringW(LOCALE_INVARIANT, LCMAP_UPPERCASE, input.c_str(),
                 static_cast<int>(input.size()), &result[0],
                 static_cast<int>(result.size()));
  return result;
}

std::string byte_string(std::size_t n)
{
  auto s = std::to_string(n);
  auto p = s.size();

  while (p > 3) {
    p -= 3;
    s.insert(p, 1, ',');
  }

  return s + " B";
}

}  // namespace usvfs::shared

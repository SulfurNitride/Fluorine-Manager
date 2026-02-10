/*
Mod Organizer BSA handling

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "bsaexception.h"

#include <cstdarg>
#include <cstdio>

#pragma warning(disable : 4996)

std::string makeString(const char* format, ...)
{
  va_list argList;
  va_start(argList, format);
  char buffer[1024];
  vsnprintf(buffer, 1024, format, argList);
  return std::string(buffer);
}

data_invalid_exception::data_invalid_exception(const std::string& message)
    : m_Message(message)
{}

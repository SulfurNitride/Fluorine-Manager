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

#ifndef BSAEXCEPTION_H
#define BSAEXCEPTION_H

#include <string>

/**
 * construct a string from a printf-style format
 * @param format printf-style format to create the string from
 * @param ... variable parameter list
 * @return the constructed string
 */
std::string makeString(const char* format, ...);

/**
 * custom exception to be thrown when invalid data is encountered
 */
class data_invalid_exception : public std::exception
{

public:
  explicit data_invalid_exception(const std::string& message);

  virtual ~data_invalid_exception() noexcept {}

  virtual const char* what() const noexcept { return m_Message.c_str(); }

private:
  std::string m_Message;
};

#endif  // BSAEXCEPTION_H

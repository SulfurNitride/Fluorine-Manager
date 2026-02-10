/*
Mod Organizer archive handling

Copyright (C) 2012 Sebastian Herbord, 2020 MO2 Team. All rights reserved.

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

#ifndef PROPERTYVARIANT_H
#define PROPERTYVARIANT_H

#ifdef _WIN32
#include <PropIdl.h>
#else
#include <Common/MyWindows.h>
#endif

/** This class implements a wrapper round the PROPVARIANT structure which
 * makes it a little friendler to use and a little more C++ish
 *
 * Sadly the inheritance needs to be public. Once you have a pointer to the
 * base class you can do pretty much anything to it anyway.
 */
class PropertyVariant : public tagPROPVARIANT
{
public:
  PropertyVariant();
  ~PropertyVariant();

  // clears the property should you wish to overwrite it with another one.
  void clear();

  bool is_empty() { return vt == VT_EMPTY; }

  // It's too much like hard work listing all the valid ones. If it doesn't
  // link, then you need to implement it...
  template <typename T>
  explicit operator T() const;

  template <typename T>
  PropertyVariant& operator=(T const&);
};

#endif  // PROPVARIANT_H

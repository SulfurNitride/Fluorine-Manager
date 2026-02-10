#ifndef ESPTYPES_H
#define ESPTYPES_H

#include <cstring>
#include <istream>

template <typename T>
static T readType(std::istream& stream)
{
  union
  {
    char buffer[sizeof(T)];
    T value;
  };
  memset(buffer, 0x42, sizeof(T));
  stream.read(buffer, sizeof(T));
  return value;
}

#endif  // ESPTYPES_H

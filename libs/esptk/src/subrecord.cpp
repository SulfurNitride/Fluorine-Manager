#include "subrecord.h"
#include "espexceptions.h"
#include "esptypes.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

ESP::SubRecord::SubRecord() : m_Type(TYPE_UNKNOWN), m_Data() {}

bool ESP::SubRecord::readFrom(std::istream& stream, uint32_t sizeOverride)
{
  static std::unordered_map<std::string, EType> s_TypeMap{{"HEDR", TYPE_HEDR},
                                                          {"CNAM", TYPE_CNAM},
                                                          {"MAST", TYPE_MAST},
                                                          {"ONAM", TYPE_ONAM},
                                                          {"SNAM", TYPE_SNAM}};

  char typeString[5];
  if (!stream.read(typeString, 4)) {
    if (stream.gcount() == 0) {
      return false;
    } else {
      throw ESP::InvalidRecordException("sub-record incomplete (unknown type)");
    }
  }
  if (stream.gcount() != 4) {
    throw ESP::InvalidRecordException(
        std::string("sub-record type incomplete (invalid type ") + typeString + ")");
  }
  typeString[4] = '\0';  // not sure if this is required, shouldn't be
  auto iter     = s_TypeMap.find(std::string(typeString));
  if (iter != s_TypeMap.end()) {
    m_Type = iter->second;
  } else if (strncmp(typeString, "XXXX", 4) == 0) {
    if (readType<uint16_t>(stream) != 4) {
      throw ESP::InvalidRecordException(
          "XXXX record is supposed to be 4 bytes in size");
    }
    return readFrom(stream, readType<uint32_t>(stream));
  } else {
    m_Type = TYPE_UNKNOWN;
  }

  uint32_t dataSize = readType<uint16_t>(stream);
  if (sizeOverride != 0UL) {
    dataSize = sizeOverride;
  }
  m_Data.resize(dataSize);

  if (dataSize > 0) {
    stream.read(reinterpret_cast<char*>(m_Data.data()), dataSize);
  }
  if (!stream) {
    throw ESP::InvalidRecordException(std::string("sub-record incomplete: ") +
                                      typeString);
  }
  return true;
}

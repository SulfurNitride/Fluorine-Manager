#include "tes3subrecord.h"
#include "espexceptions.h"
#include "esptypes.h"
#include <cstdint>
#include <string>
#include <unordered_map>

ESP::TES3SubRecord::TES3SubRecord() : m_Type(TYPE_UNKNOWN), m_Data() {}

bool ESP::TES3SubRecord::readFrom(std::istream& stream, uint32_t sizeOverride)
{
  static std::unordered_map<std::string, EType> s_TypeMap = {
      {"HEDR", TYPE_HEDR}, {"MAST", TYPE_MAST}, {"DATA", TYPE_DATA}};

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
  uint32_t dataSize;
  if (iter != s_TypeMap.end()) {
    m_Type   = iter->second;
    dataSize = readType<uint32_t>(stream);
  } else {
    m_Type   = TYPE_UNKNOWN;
    dataSize = readType<uint32_t>(stream);
    stream.seekg(8, std::istream::cur);
  }

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

#include "record.h"
#include "espexceptions.h"
#include <cstring>

ESP::Record::Record() : m_Header(), m_FormVersion(), m_Data(), m_OblivionStyle(false) {}

bool ESP::Record::flagSet(ESP::Record::EFlag flag) const
{
  return (m_Header.flags & flag) != 0;
}

bool ESP::Record::readFrom(std::istream& stream)
{
  if (!stream.read(reinterpret_cast<char*>(&m_Header), sizeof(Header))) {
    if (stream.gcount() == 0) {
      return false;
    } else {
      throw ESP::InvalidRecordException("record incomplete");
    }
  }

  char buf[4];
  stream.read(buf, 4);
  if (memcmp(buf, "HEDR", 4) == 0) {
    m_OblivionStyle = true;
    stream.seekg(-4, std::istream::cur);
    // Oblivion-style plugins don't have a form version
  } else {
    memcpy(&m_FormVersion, buf, sizeof(uint16_t));
  }

  m_Data.resize(m_Header.dataSize);
  if (m_Header.dataSize > 0) {
    stream.read(reinterpret_cast<char*>(m_Data.data()), m_Header.dataSize);
  }
  if (!stream) {
    throw ESP::InvalidRecordException("record incomplete");
  }
  return true;
}

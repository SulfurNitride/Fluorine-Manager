#include "tes3record.h"
#include "espexceptions.h"

ESP::TES3Record::TES3Record() : m_Header() {}

bool ESP::TES3Record::readFrom(std::istream& stream)
{
  if (!stream.read(reinterpret_cast<char*>(&m_Header), sizeof(Header))) {
    if (stream.gcount() == 0) {
      return false;
    } else {
      throw ESP::InvalidRecordException("record incomplete");
    }
  }
  return true;
}

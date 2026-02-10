#ifndef RECORD_H
#define RECORD_H

#include <cstdint>
#include <istream>
#include <vector>

namespace ESP
{

/**
 * @brief record storage class without record-specific information
 */
class Record
{
public:
  enum EFlag
  {
    FLAG_MASTER          = 0x00000001,
    FLAG_LIGHT_ALTERNATE = 0x00000100,  // SF light flag (FE/FF memory space)
    FLAG_LIGHT           = 0x00000200,  // SSE & FO4 light flag (FE/FF memory space)
    FLAG_OVERLAY = 0x00000200,  // SF overlay flag (does not claim new memory space,
                                // overrules light flag)
    FLAG_MEDIUM = 0x00000400,   // SF Creation update, indicates medium plugin type (FD
                                // memory space)
    FLAG_BLUEPRINT =
        0x0000800,  // SF blueprint flag (force loads after all other plugins)
    FLAG_COMPRESSED = 0x00040000
  };

public:
  Record();

  bool readFrom(std::istream& stream);

  bool flagSet(EFlag flag) const;

  uint16_t formVersion() const { return m_FormVersion; }

  const std::vector<uint8_t>& data() const { return m_Data; }

private:
  struct Header
  {
    char type[4];
    uint32_t dataSize;
    uint32_t flags;
    uint32_t id;
    uint32_t revision;
  } m_Header;

  uint16_t m_FormVersion;
  std::vector<uint8_t> m_Data;

  bool m_OblivionStyle;
};

}  // namespace ESP

#endif  // RECORD_H

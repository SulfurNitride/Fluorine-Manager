#ifndef TES3_SUBRECORD_H
#define TES3_SUBRECORD_H

#include <cstdint>
#include <istream>
#include <vector>

namespace ESP
{

/**
 * @brief sub-record storage class without record-specific information
 */
class TES3SubRecord
{
public:
  enum EType
  {
    TYPE_UNKNOWN,
    TYPE_HEDR,
    TYPE_MAST,
    TYPE_DATA
  };

  static const int NUM_TYPES = TYPE_DATA;

public:
  TES3SubRecord();

  bool readFrom(std::istream& stream, uint32_t sizeOverride = 0UL);

  EType type() const { return m_Type; }
  const std::vector<uint8_t>& data() const { return m_Data; }

private:
  EType m_Type;
  std::vector<uint8_t> m_Data;
};

}  // namespace ESP

#endif  // TES3_SUBRECORD_H

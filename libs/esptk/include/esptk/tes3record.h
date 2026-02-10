#ifndef TES3_RECORD_H
#define TES3_RECORD_H

#include <cstdint>
#include <istream>
#include <vector>

namespace ESP
{

/**
 * @brief record storage class without record-specific information
 */
class TES3Record
{
public:
  TES3Record();

  bool readFrom(std::istream& stream);

private:
  struct Header
  {
    char type[4];
    float version;
    long unknown;
  } m_Header;
};

}  // namespace ESP

#endif  // TES3_RECORD_H

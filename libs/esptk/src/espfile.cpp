#include "espfile.h"
#include "espexceptions.h"
#include "subrecord.h"
#include "tes3subrecord.h"
#include <bitset>
#include <cstring>
#include <sstream>

ESP::File::File(const std::string& fileName)
{
  m_File.open(fileName, std::fstream::in | std::fstream::binary);
  init();
}

ESP::File::File(const std::wstring& fileName)
{
#ifdef _WIN32
  m_File.open(fileName, std::fstream::in | std::fstream::binary);
#else
  // Linux: convert wstring to string (UTF-8)
  std::string narrowName(fileName.begin(), fileName.end());
  m_File.open(narrowName, std::fstream::in | std::fstream::binary);
#endif
  init();
}

class membuf : public std::basic_streambuf<char>
{
public:
  membuf(const char* start, size_t size)
  {
    // baad me! this is intended for an istream only so we're not modifying
    char* startMod = const_cast<char*>(start);
    setg(startMod, startMod, startMod + size);
  }
};

void ESP::File::init()
{
  if (!m_File.is_open()) {
    throw ESP::InvalidFileException("file not found");
  }
  m_File.exceptions(std::ios_base::badbit);

  uint8_t type[4];
  if (!m_File.read(reinterpret_cast<char*>(type), 4)) {
    throw ESP::InvalidFileException("file incomplete");
  }
  if (memcmp(type, "TES3", 4) == 0) {
    ESP::TES3Record rec;
    rec.readFrom(m_File);

    while (!m_File.eof() && !m_File.fail()) {
      ESP::TES3SubRecord subRec;
      bool success   = subRec.readFrom(m_File);
      int headerSize = sizeof(m_TES3Header);
      if (success) {
        if (subRec.type() != TES3SubRecord::TYPE_UNKNOWN) {
          switch (subRec.type()) {
          case TES3SubRecord::TYPE_HEDR:
            if (subRec.data().size() != sizeof(m_TES3Header)) {
              printf("invalid header size\n");
              m_Header.version    = 0.0f;
              m_Header.numRecords = 1;  // prevent this esp appear like a dummy
            } else {
              memcpy(&m_TES3Header, &subRec.data()[0], sizeof(m_TES3Header));
            }
            m_Header.version    = m_TES3Header.version;
            m_Header.numRecords = m_TES3Header.numRecords;
            m_Author            = reinterpret_cast<const char*>(m_TES3Header.author);
            m_Description = reinterpret_cast<const char*>(m_TES3Header.description);
            break;
          case TES3SubRecord::TYPE_MAST:
            if (subRec.data().size() > 0)
              m_Masters.insert(reinterpret_cast<const char*>(&subRec.data()[0]));
            break;
          }
        }
      }
    }
  } else if (memcmp(type, "TES4", 4) == 0) {
    m_File.seekg(0);

    m_MainRecord = readRecord();

    const std::vector<uint8_t>& data = m_MainRecord.data();
    if (data.empty()) {
      throw ESP::InvalidRecordException("record has no data");
    }
    membuf buf(reinterpret_cast<const char*>(data.data()), data.size());

    std::istream stream(&buf);
    while (!stream.eof() && !stream.fail()) {
      SubRecord rec;
      bool success = rec.readFrom(stream);
      if (success) {
        if (rec.type() != SubRecord::TYPE_UNKNOWN) {
          switch (rec.type()) {
          case SubRecord::TYPE_HEDR:
            onHEDR(rec);
            break;
          case SubRecord::TYPE_MAST:
            onMAST(rec);
            break;
          case SubRecord::TYPE_CNAM:
            onCNAM(rec);
            break;
          case SubRecord::TYPE_SNAM:
            onSNAM(rec);
            break;
          }
        }
      }
    }
  } else {
    throw ESP::InvalidFileException("invalid file type");
  }
}

void ESP::File::onHEDR(const SubRecord& rec)
{
  if (rec.data().size() != sizeof(m_Header)) {
    printf("invalid header size\n");
    m_Header.version    = 0.0f;
    m_Header.numRecords = 1;  // prevent this esp appear like a dummy
  } else {
    memcpy(&m_Header, &rec.data()[0], sizeof(m_Header));
  }
}

void ESP::File::onMAST(const SubRecord& rec)
{
  if (rec.data().size() > 0)
    m_Masters.insert(reinterpret_cast<const char*>(&rec.data()[0]));
}

void ESP::File::onCNAM(const SubRecord& rec)
{
  if (rec.data().size() > 0)
    m_Author = reinterpret_cast<const char*>(&rec.data()[0]);
}

void ESP::File::onSNAM(const SubRecord& rec)
{
  if (rec.data().size() > 0)
    m_Description = reinterpret_cast<const char*>(&rec.data()[0]);
}

ESP::Record ESP::File::readRecord()
{
  ESP::Record rec;
  rec.readFrom(m_File);
  return rec;
}

bool ESP::File::isMaster() const
{
  return m_MainRecord.flagSet(Record::FLAG_MASTER);
}

bool ESP::File::isLight(bool overlaySupport) const
{
  if (overlaySupport) {
    return m_MainRecord.flagSet(Record::FLAG_LIGHT_ALTERNATE);
  } else {
    return m_MainRecord.flagSet(Record::FLAG_LIGHT);
  }
}

bool ESP::File::isMedium() const
{
  return m_MainRecord.flagSet(Record::FLAG_MEDIUM);
}

bool ESP::File::isOverlay() const
{
  return m_MainRecord.flagSet(Record::FLAG_OVERLAY);
}

bool ESP::File::isBlueprint() const
{
  return m_MainRecord.flagSet(Record::FLAG_BLUEPRINT);
}

bool ESP::File::isDummy() const
{
  return m_Header.numRecords == 0;
}

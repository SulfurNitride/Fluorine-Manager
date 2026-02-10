#ifndef ESPFILE_H
#define ESPFILE_H

#include "record.h"
#include "tes3record.h"
#include <fstream>
#include <set>
#include <string>

namespace ESP
{

class SubRecord;

class File
{
public:
  File(const std::string& fileName);
  File(const std::wstring& fileName);

  Record readRecord();

  bool isMaster() const;
  bool isLight(bool overlaySupport = false) const;
  bool isMedium() const;
  bool isOverlay() const;
  bool isBlueprint() const;
  bool isDummy() const;
  uint16_t formVersion() const { return m_MainRecord.formVersion(); };
  float headerVersion() const { return m_Header.version; }
  std::string author() const { return m_Author; }
  std::string description() const { return m_Description; }
  std::set<std::string> masters() const { return m_Masters; }

private:
  void init();

  void onHEDR(const SubRecord& rec);
  void onMAST(const SubRecord& rec);
  void onCNAM(const SubRecord& rec);
  void onSNAM(const SubRecord& rec);

private:
  std::ifstream m_File;

  struct
  {
    float version;
    int32_t numRecords;
    uint32_t nextObjectId;
  } m_Header;

  struct
  {
    float version;
    uint32_t unknown;
    char author[32];
    char description[256];
    uint32_t numRecords;
  } m_TES3Header;

  Record m_MainRecord;

  std::string m_Author;
  std::string m_Description;

  std::set<std::string> m_Masters;
};

}  // namespace ESP

#endif  // ESPFILE_H

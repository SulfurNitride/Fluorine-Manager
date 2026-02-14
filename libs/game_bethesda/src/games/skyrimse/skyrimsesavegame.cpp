#include "skyrimsesavegame.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <QDateTime>
#include <QTimeZone>

// Windows ULARGE_INTEGER union for 64-bit FILETIME math
union _ULARGE_INTEGER {
  struct {
    uint32_t LowPart;
    uint32_t HighPart;
  };
  uint64_t QuadPart;
};

// SYSTEMTIME is _SYSTEMTIME (defined in gamebryosavegame.h for Linux)
using SYSTEMTIME = _SYSTEMTIME;

// Portable FileTimeToSystemTime: converts Windows FILETIME to SYSTEMTIME
// FILETIME = 100ns intervals since Jan 1, 1601 UTC
static void FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st)
{
  _ULARGE_INTEGER ull;
  ull.LowPart  = ft->dwLowDateTime;
  ull.HighPart = ft->dwHighDateTime;

  // 11644473600 seconds between 1601-01-01 and 1970-01-01 epochs
  int64_t unixSecs = static_cast<int64_t>(ull.QuadPart / 10000000ULL) - 11644473600LL;
  uint64_t remainderHns = ull.QuadPart % 10000000ULL;  // leftover 100ns units

  QDateTime dt = QDateTime::fromSecsSinceEpoch(unixSecs, QTimeZone::UTC);
  QDate d      = dt.date();
  QTime t      = dt.time();

  st->wYear         = static_cast<uint16_t>(d.year());
  st->wMonth        = static_cast<uint16_t>(d.month());
  st->wDayOfWeek    = static_cast<uint16_t>(d.dayOfWeek() % 7);  // Qt Mon=1..Sun=7 → Win Sun=0..Sat=6
  st->wDay          = static_cast<uint16_t>(d.day());
  st->wHour         = static_cast<uint16_t>(t.hour());
  st->wMinute       = static_cast<uint16_t>(t.minute());
  st->wSecond       = static_cast<uint16_t>(t.second());
  st->wMilliseconds = static_cast<uint16_t>(remainderHns / 10000);
}
#endif

SkyrimSESaveGame::SkyrimSESaveGame(QString const& fileName, GameSkyrimSE const* game)
    : GamebryoSaveGame(fileName, game, true)
{
  FileWrapper file(fileName, "TESV_SAVEGAME");  // 10bytes

  uint32_t version;
  FILETIME ftime;
  fetchInformationFields(file, version, m_PCName, m_PCLevel, m_PCLocation, m_SaveNumber,
                         ftime);

  // A file time is a 64-bit value that represents the number of 100-nanosecond
  // intervals that have elapsed since 12:00 A.M. January 1, 1601 Coordinated Universal
  // Time (UTC). So we need to convert that to something useful

  // For some reason, the file time is off by about 6 hours.
  // So we need to subtract those 6 hours from the filetime.
  _ULARGE_INTEGER time;
  time.LowPart  = ftime.dwLowDateTime;
  time.HighPart = ftime.dwHighDateTime;
  time.QuadPart -= 216000000000ULL;  // 6 hours in 100ns intervals
  ftime.dwHighDateTime = time.HighPart;
  ftime.dwLowDateTime  = time.LowPart;

  SYSTEMTIME ctime;
  ::FileTimeToSystemTime(&ftime, &ctime);

  setCreationTime(ctime);
}

void SkyrimSESaveGame::fetchInformationFields(FileWrapper& file, uint32_t& version,
                                              QString& playerName,
                                              unsigned short& playerLevel,
                                              QString& playerLocation,
                                              uint32_t& saveNumber,
                                              FILETIME& creationTime) const
{
  uint32_t headerSize;
  file.read(headerSize);  // header size "TESV_SAVEGAME"
  file.read(version);
  file.read(saveNumber);
  file.read(playerName);

  uint32_t temp;
  file.read(temp);
  playerLevel = static_cast<unsigned short>(temp);
  file.read(playerLocation);

  QString timeOfDay;
  file.read(timeOfDay);

  QString race;
  file.read(race);  // race name (i.e. BretonRace)

  file.skip<unsigned short>();  // Player gender (0 = male)
  file.skip<float>(2);          // experience gathered, experience required

  file.read(creationTime);  // filetime
}

std::unique_ptr<GamebryoSaveGame::DataFields> SkyrimSESaveGame::fetchDataFields() const
{
  FileWrapper file(getFilepath(), "TESV_SAVEGAME");  // 10bytes

  uint32_t version = 0;
  {
    QString dummyName, dummyLocation;
    unsigned short dummyLevel;
    uint32_t dummySaveNumber;
    FILETIME dummyTime;

    fetchInformationFields(file, version, dummyName, dummyLevel, dummyLocation,
                           dummySaveNumber, dummyTime);
  }

  std::unique_ptr<DataFields> fields = std::make_unique<DataFields>();

  uint32_t width;
  uint32_t height;
  file.read(width);
  file.read(height);

  bool alpha = false;

  // compatibility between LE and SE:
  //  SE has an additional uin16_t for compression
  //  SE uses an alpha channel, whereas LE does not
  uint16_t compressionType = 0;
  if (version == 12) {
    file.read(compressionType);
    alpha = true;
  }

  fields->Screenshot = file.readImage(width, height, 320, alpha);

  file.setCompressionType(compressionType);
  file.openCompressedData();

  uint8_t saveGameVersion = file.readChar();
  uint8_t pluginInfoSize  = file.readChar();
  uint16_t other          = file.readShort();  // Unknown

  fields->Plugins = file.readPlugins(1);  // Just empty data

  if (saveGameVersion >= 78) {
    fields->LightPlugins = file.readLightPlugins();
  }

  file.closeCompressedData();

  return fields;
}

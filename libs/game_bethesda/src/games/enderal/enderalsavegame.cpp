#include "enderalsavegame.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <QDateTime>

using SYSTEMTIME = _SYSTEMTIME;

static void FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st)
{
  const uint64_t ticks = (static_cast<uint64_t>(ft->dwHighDateTime) << 32) |
                         static_cast<uint64_t>(ft->dwLowDateTime);
  const int64_t unixSecs = static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
  const uint64_t remainderHns = ticks % 10000000ULL;

  const QDateTime dt = QDateTime::fromSecsSinceEpoch(unixSecs, Qt::UTC);
  const QDate d      = dt.date();
  const QTime t      = dt.time();

  st->wYear         = static_cast<uint16_t>(d.year());
  st->wMonth        = static_cast<uint16_t>(d.month());
  st->wDayOfWeek    = static_cast<uint16_t>(d.dayOfWeek() % 7);
  st->wDay          = static_cast<uint16_t>(d.day());
  st->wHour         = static_cast<uint16_t>(t.hour());
  st->wMinute       = static_cast<uint16_t>(t.minute());
  st->wSecond       = static_cast<uint16_t>(t.second());
  st->wMilliseconds = static_cast<uint16_t>(remainderHns / 10000);
}
#endif

#include "gameenderal.h"

EnderalSaveGame::EnderalSaveGame(QString const& fileName, GameEnderal const* game)
    : GamebryoSaveGame(fileName, game)
{
  FileWrapper file(getFilepath(), "TESV_SAVEGAME");

  FILETIME ftime;
  fetchInformationFields(file, m_SaveNumber, m_PCName, m_PCLevel, m_PCLocation, ftime);

  // A file time is a 64-bit value that represents the number of 100-nanosecond
  // intervals that have elapsed since 12:00 A.M. January 1, 1601 Coordinated Universal
  // Time (UTC). So we need to convert that to something useful
  SYSTEMTIME ctime;
  ::FileTimeToSystemTime(&ftime, &ctime);
  setCreationTime(ctime);
}

void EnderalSaveGame::fetchInformationFields(
    FileWrapper& file, uint32_t& saveNumber, QString& playerName,
    unsigned short& playerLevel, QString& playerLocation, FILETIME& creationTime) const
{
  file.skip<uint32_t>();  // header size
  file.skip<uint32_t>();  // header version
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

  file.read(creationTime);
}

std::unique_ptr<GamebryoSaveGame::DataFields> EnderalSaveGame::fetchDataFields() const
{
  FileWrapper file(getFilepath(), "TESV_SAVEGAME");
  std::unique_ptr<DataFields> fields = std::make_unique<DataFields>();

  {
    QString dummyName, dummyLocation;
    unsigned short dummyLevel;
    uint32_t dummySaveNumber;
    FILETIME dummyTime;

    fetchInformationFields(file, dummySaveNumber, dummyName, dummyLevel, dummyLocation,
                           dummyTime);
  }

  fields->Screenshot = file.readImage();

  file.skip<unsigned char>();  // form version
  file.skip<uint32_t>();  // plugin info size

  fields->Plugins = file.readPlugins();

  return fields;
}

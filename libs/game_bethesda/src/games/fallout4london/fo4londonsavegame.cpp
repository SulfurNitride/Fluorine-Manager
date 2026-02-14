#include "fo4londonsavegame.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <QDateTime>
#include <QTimeZone>

using SYSTEMTIME = _SYSTEMTIME;

static void FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st)
{
  const uint64_t ticks = (static_cast<uint64_t>(ft->dwHighDateTime) << 32) |
                         static_cast<uint64_t>(ft->dwLowDateTime);
  const int64_t unixSecs = static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
  const uint64_t remainderHns = ticks % 10000000ULL;

  const QDateTime dt = QDateTime::fromSecsSinceEpoch(unixSecs, QTimeZone::UTC);
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

#include "gamefo4london.h"

Fallout4LondonSaveGame::Fallout4LondonSaveGame(QString const& fileName,
                                               GameFallout4London const* game)
    : GamebryoSaveGame(fileName, game, true)
{
  FileWrapper file(getFilepath(), "FO4_SAVEGAME");

  FILETIME creationTime;
  fetchInformationFields(file, m_SaveNumber, m_PCName, m_PCLevel, m_PCLocation,
                         creationTime);

  // A file time is a 64-bit value that represents the number of 100-nanosecond
  // intervals that have elapsed since 12:00 A.M. January 1, 1601 Coordinated Universal
  // Time (UTC). So we need to convert that to something useful
  SYSTEMTIME ctime;
  ::FileTimeToSystemTime(&creationTime, &ctime);

  setCreationTime(ctime);
}

void Fallout4LondonSaveGame::fetchInformationFields(
    FileWrapper& file, uint32_t& saveNumber, QString& playerName,
    unsigned short& playerLevel, QString& playerLocation, FILETIME& creationTime) const
{
  file.skip<uint32_t>();  // header size
  file.skip<uint32_t>();       // header version
  file.read(saveNumber);

  file.read(playerName);

  uint32_t temp;
  file.read(temp);
  playerLevel = static_cast<unsigned short>(temp);
  file.read(playerLocation);

  QString ignore;
  file.read(ignore);  // playtime as ascii hh.mm.ss
  file.read(ignore);  // race name (i.e. BretonRace)

  file.skip<unsigned short>();  // Player gender (0 = male)
  file.skip<float>(2);          // experience gathered, experience required

  file.read(creationTime);
}

std::unique_ptr<GamebryoSaveGame::DataFields>
Fallout4LondonSaveGame::fetchDataFields() const
{
  FileWrapper file(getFilepath(), "FO4_SAVEGAME");  // 10bytes

  {
    QString dummyName, dummyLocation;
    unsigned short dummyLevel;
    uint32_t dummySaveNumber;
    FILETIME dummyTime;

    fetchInformationFields(file, dummySaveNumber, dummyName, dummyLevel, dummyLocation,
                           dummyTime);
  }

  QString ignore;
  std::unique_ptr<DataFields> fields = std::make_unique<DataFields>();

  fields->Screenshot = file.readImage(384, true);

  uint8_t saveGameVersion = file.readChar();
  file.read(ignore);      // game version
  file.skip<uint32_t>();  // plugin info size

  fields->Plugins = file.readPlugins();
  if (saveGameVersion >= 68) {
    fields->LightPlugins = file.readLightPlugins();
  }

  return fields;
}

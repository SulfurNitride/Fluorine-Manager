#include "gamebryosavegame.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QTemporaryFile>

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace
{
class FileWrapperAccess : public GamebryoSaveGame
{
public:
  static std::pair<uint8_t, uint16_t> readZlibBlock(const QString& path)
  {
    FileWrapper file(path, QStringLiteral("SAVE"));
    file.setCompressionType(1);
    file.openCompressedData();
    const uint8_t first = file.readChar();
    const uint16_t second = file.readShort();
    file.closeCompressedData();
    return {first, second};
  }
};

void appendU32(QByteArray& output, uint32_t value)
{
  char encoded[sizeof(value)];
  std::memcpy(encoded, &value, sizeof(value));
  output.append(encoded, sizeof(encoded));
}

QByteArray zlibSaveBlock(QByteArray payload, bool truncate)
{
  QByteArray compressed = qCompress(payload, 9).mid(4);
  if (truncate) {
    compressed.chop(1);
  }

  QByteArray contents("SAVE", 4);
  appendU32(contents, static_cast<uint32_t>(payload.size()));
  appendU32(contents, static_cast<uint32_t>(compressed.size()));
  contents.append(compressed);
  return contents;
}

QString writeTemporarySave(QTemporaryFile& file, const QByteArray& contents)
{
  EXPECT_TRUE(file.open());
  EXPECT_EQ(file.write(contents), contents.size());
  EXPECT_TRUE(file.flush());
  return file.fileName();
}
}

TEST(GamebryoSaveCompression, ReadsSingleStreamZlibBlock)
{
  QByteArray payload;
  payload.append(static_cast<char>(78));
  const uint16_t marker = 0x1234;
  payload.append(reinterpret_cast<const char*>(&marker), sizeof(marker));

  QTemporaryFile file;
  const QString path = writeTemporarySave(file, zlibSaveBlock(payload, false));
  const auto [first, second] = FileWrapperAccess::readZlibBlock(path);

  EXPECT_EQ(first, 78);
  EXPECT_EQ(second, marker);
}

TEST(GamebryoSaveCompression, RejectsTruncatedZlibBlock)
{
  QTemporaryFile file;
  const QString path = writeTemporarySave(
      file, zlibSaveBlock(QByteArray("save metadata"), true));

  EXPECT_THROW(FileWrapperAccess::readZlibBlock(path), std::runtime_error);
}

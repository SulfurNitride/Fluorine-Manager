#include "pefileutils.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace
{
QByteArray minimalPe(quint16 characteristics = 0)
{
  QByteArray contents(128, '\0');
  contents[0] = 'M';
  contents[1] = 'Z';
  contents[0x3c] = 0x40;
  contents.replace(0x40, 4, QByteArrayLiteral("PE\0\0"));
  contents[0x40 + 22] = static_cast<char>(characteristics & 0xffu);
  contents[0x40 + 23] = static_cast<char>((characteristics >> 8) & 0xffu);
  return contents;
}

QString writeFile(const QString& root, const QString& name,
                  const QByteArray& contents)
{
  const QString path = root + QLatin1Char('/') + name;
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::WriteOnly));
  EXPECT_EQ(file.write(contents), contents.size());
  file.close();
  return path;
}
}

TEST(PeFileUtils, SetsLargeAddressAwareAndPreservesBackup)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QByteArray original = minimalPe();
  const QString path = writeFile(temporary.path(), QStringLiteral("Oblivion.exe"),
                                 original);
  EXPECT_FALSE(peLargeAddressAware(path));

  QString error;
  ASSERT_TRUE(setPeLargeAddressAware(path, &error)) << error.toStdString();
  EXPECT_TRUE(peLargeAddressAware(path));
  QFile backup(path + QStringLiteral(".fluorine-unpatched"));
  ASSERT_TRUE(backup.open(QIODevice::ReadOnly));
  EXPECT_EQ(backup.readAll(), original);
}

TEST(PeFileUtils, IsIdempotentForAnAlreadyPatchedExecutable)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = writeFile(temporary.path(), QStringLiteral("patched.exe"),
                                 minimalPe(0x20));
  EXPECT_TRUE(setPeLargeAddressAware(path));
  EXPECT_TRUE(peLargeAddressAware(path));
  EXPECT_FALSE(QFile::exists(path + QStringLiteral(".fluorine-unpatched")));
}

TEST(PeFileUtils, RejectsMalformedInput)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString path = writeFile(temporary.path(), QStringLiteral("not-pe.exe"),
                                 QByteArrayLiteral("not a PE"));
  QString error;
  EXPECT_FALSE(setPeLargeAddressAware(path, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_FALSE(peLargeAddressAware(path));
}

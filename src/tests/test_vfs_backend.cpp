#include "usvfsrequest.h"
#include "vfsbackend.h"

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace
{
quint32 readU32(const QByteArray& bytes, qsizetype& offset)
{
  EXPECT_LE(offset + 4, bytes.size());
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + offset);
  offset += 4;
  return static_cast<quint32>(p[0]) |
         (static_cast<quint32>(p[1]) << 8) |
         (static_cast<quint32>(p[2]) << 16) |
         (static_cast<quint32>(p[3]) << 24);
}

QString readString(const QByteArray& bytes, qsizetype& offset)
{
  const quint32 length = readU32(bytes, offset);
  EXPECT_LE(offset + static_cast<qsizetype>(length), bytes.size());
  const QString value =
      QString::fromUtf8(bytes.constData() + offset, static_cast<qsizetype>(length));
  offset += static_cast<qsizetype>(length);
  return value;
}
}

TEST(VfsBackend, ParsesKnownAndSafeDefaultValues)
{
  EXPECT_EQ(VfsBackend::Fuse, parseVfsBackend({}));
  EXPECT_EQ(VfsBackend::Fuse, parseVfsBackend(QStringLiteral("unknown")));
  EXPECT_EQ(VfsBackend::Usvfs, parseVfsBackend(QStringLiteral("usvfs")));
  EXPECT_EQ(VfsBackend::Usvfs, parseVfsBackend(QStringLiteral("USVFS")));
  EXPECT_EQ(QStringLiteral("fuse"), vfsBackendSettingValue(VfsBackend::Fuse));
  EXPECT_EQ(QStringLiteral("usvfs"), vfsBackendSettingValue(VfsBackend::Usvfs));
}

TEST(VfsBackend, UsesUsvfsOnlyForWineOrganizerVfsLaunches)
{
  EXPECT_FALSE(useUsvfsForLaunch(VfsBackend::Fuse, true, true));
  EXPECT_FALSE(useUsvfsForLaunch(VfsBackend::Usvfs, false, true));
  EXPECT_FALSE(useUsvfsForLaunch(VfsBackend::Usvfs, true, false));
  EXPECT_TRUE(useUsvfsForLaunch(VfsBackend::Usvfs, true, true));
}

TEST(VfsBackend, ParsesExperimentOverridesWithoutUnsafeImplicitEnable)
{
  EXPECT_TRUE(parseUsvfsExperimentFlag(QStringLiteral("1"), false));
  EXPECT_TRUE(parseUsvfsExperimentFlag(QStringLiteral(" TRUE "), false));
  EXPECT_TRUE(parseUsvfsExperimentFlag(QStringLiteral("on"), false));
  EXPECT_FALSE(parseUsvfsExperimentFlag(QStringLiteral("0"), true));
  EXPECT_FALSE(parseUsvfsExperimentFlag(QStringLiteral("False"), true));
  EXPECT_FALSE(parseUsvfsExperimentFlag(QStringLiteral(" off "), true));
  EXPECT_FALSE(parseUsvfsExperimentFlag(QStringLiteral("enable"), false));
  EXPECT_TRUE(parseUsvfsExperimentFlag(QStringLiteral("enable"), true));
  EXPECT_FALSE(parseUsvfsExperimentFlag({}, false));
  EXPECT_TRUE(parseUsvfsExperimentFlag({}, true));
}

TEST(VfsBackend, UsesHelperAsUsvfsProcessLifetimeAnchor)
{
  const QStringList targetExecutables{QStringLiteral("skse64_loader.exe"),
                                      QStringLiteral("SkyrimSE.exe")};

  EXPECT_EQ(targetExecutables,
            processTrackingExecutables(targetExecutables, false));
  EXPECT_EQ(QStringList{QStringLiteral("fluorine-usvfs-launcher.exe")},
            processTrackingExecutables(targetExecutables, true));
}

TEST(VfsBackend, ConvertsHostPathsForWine)
{
  EXPECT_EQ(QStringLiteral("Z:\\games\\Skyrim SE"),
            toWinePath(QStringLiteral("/games/Skyrim SE")));
  EXPECT_EQ(QStringLiteral("C:\\Games\\SkyrimSE.exe"),
            toWinePath(QStringLiteral("C:/Games/SkyrimSE.exe")));
  EXPECT_EQ(QStringLiteral("relative\\file"),
            toWinePath(QStringLiteral("relative/file")));
}

TEST(UsvfsRequest, WritesVersionedLengthPrefixedRequest)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());

  UsvfsRequestOptions options;
  options.binary = QFileInfo(temporary.filePath(QStringLiteral("game.exe")));
  options.workingDirectory = QDir(temporary.path());
  options.arguments = {QStringLiteral("--profile"), QStringLiteral("Test Profile"),
                       QString{}, QString::fromUtf8("Grüße 🧪"),
                       QStringLiteral("$HOME;*.esp'\"\\")};
  options.mappings = {
      {temporary.filePath(QStringLiteral("mod")),
       temporary.filePath(QStringLiteral("game/Data")), true, true},
  };
  options.forcedLibraries = {
      MOBase::ExecutableForcedLoadSetting(
          QStringLiteral("game.exe"),
          temporary.filePath(QStringLiteral("enabled-hook.dll")))
          .withEnabled(),
      MOBase::ExecutableForcedLoadSetting(
          QStringLiteral("disabled.exe"),
          temporary.filePath(QStringLiteral("disabled-hook.dll"))),
  };
  options.executableBlacklist = {QStringLiteral("blocked.exe")};
  options.skipFileSuffixes = {QStringLiteral(".mohidden")};
  options.skipDirectories = {QStringLiteral(".git")};
  options.logPath = temporary.filePath(QStringLiteral("usvfs.log"));

  const UsvfsRequestResult result = writeUsvfsRequest(options);
  ASSERT_TRUE(result) << result.error.toStdString();
  ASSERT_FALSE(result.instanceName.isEmpty());

  QFile request(result.path);
  ASSERT_TRUE(request.open(QIODevice::ReadOnly));
  const QByteArray bytes = request.readAll();
  request.close();
  EXPECT_TRUE(QFile::remove(result.path));

  ASSERT_GE(bytes.size(), 12);
  EXPECT_EQ(QByteArray("FUSVFS1\0", 8), bytes.first(8));
  qsizetype offset = 8;
  EXPECT_EQ(2U, readU32(bytes, offset));
  EXPECT_EQ(result.instanceName, readString(bytes, offset));
  EXPECT_EQ(toWinePath(options.binary.absoluteFilePath()),
            readString(bytes, offset));
  EXPECT_EQ(toWinePath(options.workingDirectory.absolutePath()),
            readString(bytes, offset));
  EXPECT_EQ(toWinePath(options.logPath), readString(bytes, offset));
  EXPECT_EQ(5U, readU32(bytes, offset));
  EXPECT_EQ(QStringLiteral("--profile"), readString(bytes, offset));
  EXPECT_EQ(QStringLiteral("Test Profile"), readString(bytes, offset));
  EXPECT_EQ(QString{}, readString(bytes, offset));
  EXPECT_EQ(QString::fromUtf8("Grüße 🧪"), readString(bytes, offset));
  EXPECT_EQ(QStringLiteral("$HOME;*.esp'\"\\"), readString(bytes, offset));
  EXPECT_EQ(1U, readU32(bytes, offset));
  ASSERT_LT(offset + 3, bytes.size());
  EXPECT_EQ(1, bytes.at(offset++));
  EXPECT_EQ(1, bytes.at(offset++));
  EXPECT_EQ(0, bytes.at(offset++));
  EXPECT_EQ(toWinePath(options.mappings.front().source), readString(bytes, offset));
  EXPECT_EQ(toWinePath(options.mappings.front().destination),
            readString(bytes, offset));
  EXPECT_EQ(0U, readU32(bytes, offset));
  EXPECT_EQ(1U, readU32(bytes, offset));
  EXPECT_EQ(QStringLiteral("game.exe"), readString(bytes, offset));
  EXPECT_EQ(toWinePath(temporary.filePath(QStringLiteral("enabled-hook.dll"))),
            readString(bytes, offset));
  EXPECT_EQ(1U, readU32(bytes, offset));
  EXPECT_EQ(QStringLiteral("blocked.exe"), readString(bytes, offset));
  EXPECT_EQ(1U, readU32(bytes, offset));
  EXPECT_EQ(QStringLiteral(".mohidden"), readString(bytes, offset));
  EXPECT_EQ(1U, readU32(bytes, offset));
  EXPECT_EQ(QStringLiteral(".git"), readString(bytes, offset));
  EXPECT_EQ(bytes.size(), offset);
}

TEST(UsvfsRequest, MarksDataMappingsAroundResolvedSnapshot)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString data = temporary.filePath(QStringLiteral("game/Data"));

  UsvfsRequestOptions options;
  options.binary = QFileInfo(temporary.filePath(QStringLiteral("game.exe")));
  options.workingDirectory = QDir(temporary.path());
  options.mappings = {
      {temporary.filePath(QStringLiteral("mod")), data, true, false},
      {temporary.filePath(QStringLiteral("plugins.txt")),
       QDir(data).filePath(QStringLiteral("plugins.txt")), false, false},
      {temporary.filePath(QStringLiteral("saves")),
       temporary.filePath(QStringLiteral("Documents/Saves")), true, true},
  };
  options.useResolvedSnapshot = true;
  options.dataDirectory = data;
  options.resolvedMappings = {
      {temporary.filePath(QStringLiteral("mod/meshes")),
       QDir(data).filePath(QStringLiteral("meshes")), true, false},
      {temporary.filePath(QStringLiteral("mod/meshes/a.nif")),
       QDir(data).filePath(QStringLiteral("meshes/a.nif")), false, false},
  };

  const UsvfsRequestResult result = writeUsvfsRequest(options);
  ASSERT_TRUE(result) << result.error.toStdString();
  QFile request(result.path);
  ASSERT_TRUE(request.open(QIODevice::ReadOnly));
  const QByteArray bytes = request.readAll();
  request.close();
  EXPECT_TRUE(QFile::remove(result.path));

  qsizetype offset = 8;
  ASSERT_EQ(2U, readU32(bytes, offset));
  for (int i = 0; i < 4; ++i) (void)readString(bytes, offset);
  ASSERT_EQ(0U, readU32(bytes, offset));
  ASSERT_EQ(3U, readU32(bytes, offset));

  EXPECT_EQ(1, bytes.at(offset++));
  EXPECT_EQ(0, bytes.at(offset++));
  EXPECT_EQ(1, bytes.at(offset++));  // shallow Data directory
  (void)readString(bytes, offset);
  (void)readString(bytes, offset);

  EXPECT_EQ(0, bytes.at(offset++));
  EXPECT_EQ(0, bytes.at(offset++));
  EXPECT_EQ(2, bytes.at(offset++));  // file applied after snapshot
  (void)readString(bytes, offset);
  (void)readString(bytes, offset);

  EXPECT_EQ(1, bytes.at(offset++));
  EXPECT_EQ(1, bytes.at(offset++));
  EXPECT_EQ(0, bytes.at(offset++));  // non-Data mapping remains ordinary
  (void)readString(bytes, offset);
  (void)readString(bytes, offset);

  ASSERT_EQ(2U, readU32(bytes, offset));
  EXPECT_EQ(1, bytes.at(offset++));
  (void)readString(bytes, offset);
  (void)readString(bytes, offset);
  EXPECT_EQ(0, bytes.at(offset++));
  (void)readString(bytes, offset);
  (void)readString(bytes, offset);
}

#include "downloadmetadatapolicy.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {
QString metadataPath(const QTemporaryDir &directory) {
  return directory.filePath(QStringLiteral("archive.7z.meta"));
}

QVariantMap capabilityUserData() {
  return {
      {QStringLiteral("downloadMap"),
       QVariantList{QVariantMap{
           {QStringLiteral("URI"),
            QStringLiteral("https://cdn.invalid/object?token=SECRET")},
           {QStringLiteral("short_name"), QStringLiteral("CDN")}}}},
      {QStringLiteral("foreign"), QStringLiteral("preserve me")},
  };
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}
} // namespace

TEST(DownloadMetadataPolicy, DownloadPhaseClassificationIsExhaustive) {
  using DownloadMetadataPolicy::CapabilityRetention;
  using DownloadMetadataPolicy::DownloadPhase;
  const std::pair<DownloadPhase, CapabilityRetention> cases[]{
      {DownloadPhase::Started, CapabilityRetention::Resumable},
      {DownloadPhase::Downloading, CapabilityRetention::Resumable},
      {DownloadPhase::Canceling, CapabilityRetention::Resumable},
      {DownloadPhase::Pausing, CapabilityRetention::Resumable},
      {DownloadPhase::Canceled, CapabilityRetention::Retire},
      {DownloadPhase::Paused, CapabilityRetention::Resumable},
      {DownloadPhase::Error, CapabilityRetention::Resumable},
      {DownloadPhase::FetchingModInfo, CapabilityRetention::Retire},
      {DownloadPhase::FetchingFileInfo, CapabilityRetention::Retire},
      {DownloadPhase::FetchingModInfoMd5, CapabilityRetention::Retire},
      {DownloadPhase::NoFetch, CapabilityRetention::Retire},
      {DownloadPhase::Ready, CapabilityRetention::Retire},
      {DownloadPhase::Installed, CapabilityRetention::Retire},
      {DownloadPhase::Uninstalled, CapabilityRetention::Retire},
  };

  for (const auto &[phase, expected] : cases) {
    EXPECT_EQ(DownloadMetadataPolicy::retentionForPhase(phase), expected);
  }
}

TEST(DownloadMetadataPolicy, FinalArchiveRequiresSuccessfulPartialRename) {
  using DownloadMetadataPolicy::CapabilityRetention;
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString partial =
      directory.filePath(QStringLiteral("archive.7z.unfinished"));
  const QString final = directory.filePath(QStringLiteral("archive.7z"));

  QFile output(partial);
  ASSERT_TRUE(output.open(QIODevice::WriteOnly));
  ASSERT_EQ(output.write("archive"), 7);
  output.close();
  EXPECT_EQ(DownloadMetadataPolicy::retentionAfterPublication(partial, partial),
            CapabilityRetention::Resumable);
  EXPECT_EQ(DownloadMetadataPolicy::retentionAfterPublication(final, partial),
            CapabilityRetention::Resumable);

  ASSERT_TRUE(output.rename(final));
  EXPECT_EQ(DownloadMetadataPolicy::retentionAfterPublication(final, partial),
            CapabilityRetention::Retire);
  EXPECT_EQ(DownloadMetadataPolicy::retentionAfterPublication(partial, partial),
            CapabilityRetention::Resumable);
}

TEST(DownloadMetadataPolicy, PartialSuffixIsReservedAtAdmission) {
  const QString supported = DownloadMetadataPolicy::unambiguousFinalBaseName(
      QStringLiteral("archive.7z.unfinished"));
  EXPECT_EQ(supported, QStringLiteral("archive.7z"));
  EXPECT_EQ(QFileInfo(supported).suffix(), QStringLiteral("7z"));
  EXPECT_EQ(DownloadMetadataPolicy::unambiguousFinalBaseName(
                QStringLiteral("archive.unfinished")),
            QStringLiteral("archive"));
  EXPECT_EQ(DownloadMetadataPolicy::unambiguousFinalBaseName(
                QStringLiteral("archive.unfinished.unfinished")),
            QStringLiteral("archive"));
  EXPECT_EQ(DownloadMetadataPolicy::unambiguousFinalBaseName(
                QStringLiteral(".unfinished")),
            QStringLiteral("download"));
  EXPECT_EQ(DownloadMetadataPolicy::unambiguousFinalBaseName(supported),
            supported);
  EXPECT_EQ(DownloadMetadataPolicy::unambiguousFinalBaseName(
                QStringLiteral("archive.zip")),
            QStringLiteral("archive.zip"));
}

TEST(DownloadMetadataPolicy, ResumableMetadataRetainsExactCapabilities) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);
  const QStringList urls{
      QStringLiteral("https://cdn.invalid/path?token=SECRET_ONE"),
      QStringLiteral("https://cdn.invalid/path?token=SECRET_TWO")};

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("foreignSetting"), QStringLiteral("keep"));
    DownloadMetadataPolicy::write(
        settings, DownloadMetadataPolicy::CapabilityRetention::Resumable, urls,
        capabilityUserData());
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  QSettings settings(path, QSettings::IniFormat);
  const auto loaded = DownloadMetadataPolicy::loadAndConverge(
      settings, DownloadMetadataPolicy::CapabilityRetention::Resumable);
  EXPECT_FALSE(loaded.changed);
  EXPECT_EQ(loaded.status, QSettings::NoError);
  EXPECT_EQ(loaded.urls, urls);
  EXPECT_TRUE(loaded.userData.contains(QStringLiteral("downloadMap")));
  EXPECT_EQ(settings.value(QStringLiteral("foreignSetting")).toString(),
            QStringLiteral("keep"));
}

TEST(DownloadMetadataPolicy, TerminalWriteRemovesOnlyCapabilities) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);

  QSettings settings(path, QSettings::IniFormat);
  settings.setValue(QStringLiteral("url"),
                    QStringLiteral("https://old.invalid/?token=OLD_SECRET"));
  settings.setValue(QStringLiteral("foreignSetting"), QStringLiteral("keep"));
  DownloadMetadataPolicy::write(
      settings, DownloadMetadataPolicy::CapabilityRetention::Retire,
      {QStringLiteral("https://new.invalid/?token=NEW_SECRET")},
      capabilityUserData());
  settings.sync();
  ASSERT_EQ(settings.status(), QSettings::NoError);

  EXPECT_FALSE(settings.contains(QStringLiteral("url")));
  const QVariantMap userData =
      settings.value(QStringLiteral("userData")).toMap();
  EXPECT_FALSE(userData.contains(QStringLiteral("downloadMap")));
  EXPECT_EQ(userData.value(QStringLiteral("foreign")).toString(),
            QStringLiteral("preserve me"));
  EXPECT_EQ(settings.value(QStringLiteral("foreignSetting")).toString(),
            QStringLiteral("keep"));
}

TEST(DownloadMetadataPolicy, LegacyTerminalMetadataConvergesOnLoad) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("url"),
                      QStringLiteral("https://cdn.invalid/?token=SECRET"));
    settings.setValue(QStringLiteral("userData"), capabilityUserData());
    settings.setValue(QStringLiteral("installed"), true);
    settings.setValue(QStringLiteral("repository"), QStringLiteral("Nexus"));
    settings.sync();
    ASSERT_EQ(settings.status(), QSettings::NoError);
  }

  {
    QSettings settings(path, QSettings::IniFormat);
    const auto loaded = DownloadMetadataPolicy::loadAndConverge(
        settings, DownloadMetadataPolicy::CapabilityRetention::Retire);
    EXPECT_TRUE(loaded.changed);
    EXPECT_TRUE(loaded.urls.isEmpty());
    EXPECT_FALSE(loaded.userData.contains(QStringLiteral("downloadMap")));
    EXPECT_EQ(loaded.userData.value(QStringLiteral("foreign")).toString(),
              QStringLiteral("preserve me"));
    EXPECT_EQ(loaded.status, QSettings::NoError);
  }

  QSettings persisted(path, QSettings::IniFormat);
  EXPECT_FALSE(persisted.contains(QStringLiteral("url")));
  EXPECT_FALSE(persisted.value(QStringLiteral("userData"))
                   .toMap()
                   .contains(QStringLiteral("downloadMap")));
  EXPECT_TRUE(persisted.value(QStringLiteral("installed")).toBool());
  EXPECT_EQ(persisted.value(QStringLiteral("repository")).toString(),
            QStringLiteral("Nexus"));
}

TEST(DownloadMetadataPolicy, TerminalLoadPreservesUnknownUserDataShape) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);

  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("url"),
                      QStringLiteral("https://cdn.invalid/?token=SECRET"));
    settings.setValue(QStringLiteral("userData"),
                      QStringLiteral("foreign shape"));
    settings.sync();
  }

  QSettings settings(path, QSettings::IniFormat);
  const auto loaded = DownloadMetadataPolicy::loadAndConverge(
      settings, DownloadMetadataPolicy::CapabilityRetention::Retire);
  EXPECT_TRUE(loaded.changed);
  EXPECT_FALSE(settings.contains(QStringLiteral("url")));
  EXPECT_EQ(settings.value(QStringLiteral("userData")).toString(),
            QStringLiteral("foreign shape"));
}

TEST(DownloadMetadataPolicy, HiddenTerminalRetiresButHiddenPartialRetains) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString terminalPath =
      directory.filePath(QStringLiteral("terminal.meta"));
  const QString partialPath =
      directory.filePath(QStringLiteral("partial.meta"));

  for (const QString &path : {terminalPath, partialPath}) {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("removed"), true);
    settings.setValue(QStringLiteral("url"),
                      QStringLiteral("https://cdn.invalid/?token=SECRET"));
    settings.setValue(QStringLiteral("userData"), capabilityUserData());
    settings.sync();
  }

  {
    QSettings terminal(terminalPath, QSettings::IniFormat);
    const auto loaded = DownloadMetadataPolicy::loadAndConverge(
        terminal, DownloadMetadataPolicy::CapabilityRetention::Retire);
    EXPECT_TRUE(loaded.changed);
    EXPECT_TRUE(terminal.value(QStringLiteral("removed")).toBool());
    EXPECT_FALSE(terminal.contains(QStringLiteral("url")));
  }
  {
    QSettings partial(partialPath, QSettings::IniFormat);
    const auto loaded = DownloadMetadataPolicy::loadAndConverge(
        partial, DownloadMetadataPolicy::CapabilityRetention::Resumable);
    EXPECT_FALSE(loaded.changed);
    EXPECT_FALSE(loaded.urls.isEmpty());
    EXPECT_TRUE(loaded.userData.contains(QStringLiteral("downloadMap")));
  }
}

TEST(DownloadMetadataPolicy, TerminalConvergenceIsIdempotent) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  QSettings settings(metadataPath(directory), QSettings::IniFormat);
  DownloadMetadataPolicy::write(
      settings, DownloadMetadataPolicy::CapabilityRetention::Retire, {},
      {{QStringLiteral("foreign"), QStringLiteral("keep")}});
  settings.sync();

  const auto first = DownloadMetadataPolicy::loadAndConverge(
      settings, DownloadMetadataPolicy::CapabilityRetention::Retire);
  const auto second = DownloadMetadataPolicy::loadAndConverge(
      settings, DownloadMetadataPolicy::CapabilityRetention::Retire);
  EXPECT_FALSE(first.changed);
  EXPECT_FALSE(second.changed);
  EXPECT_EQ(second.userData.value(QStringLiteral("foreign")).toString(),
            QStringLiteral("keep"));
}

TEST(DownloadMetadataPolicy, DiscardedMetadataFileIsRemoved) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);
  {
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("url"),
                      QStringLiteral("https://cdn.invalid/?token=SECRET"));
    settings.setValue(QStringLiteral("userData"), capabilityUserData());
    settings.sync();
  }

  EXPECT_TRUE(DownloadMetadataPolicy::retireFile(path));
  EXPECT_FALSE(QFileInfo::exists(path));
  EXPECT_TRUE(DownloadMetadataPolicy::retireFile(path));
}

TEST(DownloadMetadataPolicy, MalformedTerminalMetadataIsUnlinked) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);
  QFile malformed(path);
  ASSERT_TRUE(malformed.open(QIODevice::WriteOnly));
  ASSERT_GT(malformed.write("url=https://cdn.invalid/?token=SECRET\n%broken"),
            0);
  malformed.close();

  EXPECT_TRUE(DownloadMetadataPolicy::retireFile(path));
  EXPECT_FALSE(QFileInfo::exists(path));
}

#ifdef Q_OS_UNIX
TEST(DownloadMetadataPolicy, TerminalConvergenceRefusesSymlinkLeaf) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString externalPath =
      directory.filePath(QStringLiteral("external.meta"));
  const QString managedPath = metadataPath(directory);

  {
    QSettings external(externalPath, QSettings::IniFormat);
    external.setValue(QStringLiteral("url"),
                      QStringLiteral("https://cdn.invalid/?token=SECRET"));
    external.setValue(QStringLiteral("userData"), capabilityUserData());
    external.sync();
    ASSERT_EQ(external.status(), QSettings::NoError);
  }
  const QByteArray before = readFile(externalPath);
  ASSERT_FALSE(before.isEmpty());
  ASSERT_TRUE(QFile::link(externalPath, managedPath));

  {
    QSettings managed(managedPath, QSettings::IniFormat);
    const auto loaded = DownloadMetadataPolicy::loadAndConverge(
        managed, DownloadMetadataPolicy::CapabilityRetention::Retire);
    EXPECT_FALSE(loaded.changed);
    EXPECT_TRUE(loaded.urls.isEmpty());
    EXPECT_FALSE(loaded.userData.contains(QStringLiteral("downloadMap")));
    EXPECT_EQ(loaded.status, QSettings::AccessError);
  }

  EXPECT_EQ(readFile(externalPath), before);
  EXPECT_TRUE(QFileInfo(managedPath).isSymLink());
}

TEST(DownloadMetadataPolicy, MetadataLeafGateRejectsFifoBeforeQSettings) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = metadataPath(directory);
  ASSERT_EQ(::mkfifo(QFile::encodeName(path).constData(), 0600), 0);

  EXPECT_FALSE(DownloadMetadataPolicy::isSafeMetadataLeaf(path));
  EXPECT_TRUE(QFileInfo(path).exists());
  EXPECT_FALSE(QFileInfo(path).isFile());
}

TEST(DownloadMetadataPolicy, DiscardedSymlinkDoesNotTouchTarget) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString externalPath =
      directory.filePath(QStringLiteral("external-discard.meta"));
  const QString managedPath = metadataPath(directory);
  {
    QFile external(externalPath);
    ASSERT_TRUE(external.open(QIODevice::WriteOnly));
    ASSERT_EQ(external.write("SECRET"), 6);
  }
  ASSERT_TRUE(QFile::link(externalPath, managedPath));

  EXPECT_TRUE(DownloadMetadataPolicy::retireFile(managedPath));
  EXPECT_FALSE(QFileInfo(managedPath).isSymLink());
  EXPECT_EQ(readFile(externalPath), QByteArray("SECRET"));
}
#endif

#include "instanceunregister.h"

#include <QDir>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>

#ifdef Q_OS_LINUX
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

bool writeBytes(const QString& path, QByteArrayView bytes)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes.data(), bytes.size()) == bytes.size() && file.flush();
}

QByteArray readBytes(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

std::atomic_bool rejectCustomSettingsWrites{false};

bool readCustomSettings(QIODevice& device, QSettings::SettingsMap& values)
{
  QDataStream stream(&device);
  stream >> values;
  return stream.status() == QDataStream::Ok;
}

bool writeCustomSettings(QIODevice& device,
                         const QSettings::SettingsMap& values)
{
  if (rejectCustomSettingsWrites.load()) {
    return false;
  }
  QDataStream stream(&device);
  stream << values;
  return stream.status() == QDataStream::Ok;
}

QSettings::Format failureInjectingFormat()
{
  static const QSettings::Format format = QSettings::registerFormat(
      QStringLiteral("fluorine-registry-test"), readCustomSettings,
      writeCustomSettings, Qt::CaseSensitive);
  return format;
}

}  // namespace

TEST(InstanceUnregister, DisablesGlobalIniWithoutChangingItsGeneration)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("ModOrganizer.ini");
  const QString disabled = source + QStringLiteral(".disabled");
  ASSERT_TRUE(writeBytes(source, "[General]\nvalue=sentinel\n"));
#ifdef Q_OS_LINUX
  ASSERT_EQ(::chmod(QFile::encodeName(source).constData(), 0640), 0);
#endif

  const auto result = InstanceUnregister::disableGlobalIni(source);
  ASSERT_TRUE(result) << qPrintable(result.error);
  EXPECT_EQ(result.status, InstanceUnregister::DisableStatus::Disabled);
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readBytes(disabled), "[General]\nvalue=sentinel\n");
#ifdef Q_OS_LINUX
  struct stat status{};
  ASSERT_EQ(::stat(QFile::encodeName(disabled).constData(), &status), 0);
  EXPECT_EQ(status.st_mode & 0777, 0640);
#endif
}

TEST(InstanceUnregister, ExistingDisabledIniIsNeverReplaced)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("ModOrganizer.ini");
  const QString disabled = source + QStringLiteral(".disabled");
  ASSERT_TRUE(writeBytes(source, "live\n"));
  ASSERT_TRUE(writeBytes(disabled, "existing-disabled\n"));

  const auto result = InstanceUnregister::disableGlobalIni(source);
  EXPECT_EQ(result.status, InstanceUnregister::DisableStatus::DestinationExists);
  EXPECT_EQ(readBytes(source), "live\n");
  EXPECT_EQ(readBytes(disabled), "existing-disabled\n");
}

#ifdef Q_OS_LINUX
TEST(InstanceUnregister, DanglingDestinationAndSymlinkSourceArePreserved)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("ModOrganizer.ini");
  const QString disabled = source + QStringLiteral(".disabled");
  ASSERT_TRUE(writeBytes(source, "live\n"));
  ASSERT_EQ(::symlink("missing-target",
                      QFile::encodeName(disabled).constData()),
            0);

  auto result = InstanceUnregister::disableGlobalIni(source);
  EXPECT_EQ(result.status, InstanceUnregister::DisableStatus::DestinationExists);
  EXPECT_EQ(readBytes(source), "live\n");
  EXPECT_TRUE(QFileInfo(disabled).isSymLink());

  ASSERT_TRUE(QFile::remove(disabled));
  ASSERT_TRUE(QFile::remove(source));
  ASSERT_TRUE(writeBytes(temporary.filePath("real.ini"), "real\n"));
  ASSERT_EQ(::symlink("real.ini", QFile::encodeName(source).constData()), 0);
  result = InstanceUnregister::disableGlobalIni(source);
  EXPECT_EQ(result.status, InstanceUnregister::DisableStatus::SourceUnsafe);
  EXPECT_TRUE(QFileInfo(source).isSymLink());
  EXPECT_FALSE(QFileInfo::exists(disabled));
}
#endif

TEST(InstanceUnregister, MissingSourceFailsWithoutCreatingDestination)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = temporary.filePath("ModOrganizer.ini");
  const auto result = InstanceUnregister::disableGlobalIni(source);
  EXPECT_EQ(result.status, InstanceUnregister::DisableStatus::SourceUnavailable);
  EXPECT_FALSE(QFileInfo::exists(source + QStringLiteral(".disabled")));
}

TEST(InstanceUnregister, PortableRegistryIsSynchronizedAndVerified)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString settingsPath = temporary.filePath("global.ini");
  const QString portablePath = temporary.filePath("portable");
  QSettings settings(settingsPath, QSettings::IniFormat);

  auto result = InstanceUnregister::updatePortableRegistration(
      settings, portablePath, /*registered=*/true);
  ASSERT_TRUE(result) << qPrintable(result.error);
  QSettings added(settingsPath, QSettings::IniFormat);
  EXPECT_EQ(added.value("PortableInstances").toStringList(),
            QStringList{QFileInfo(portablePath).absoluteFilePath()});

  result = InstanceUnregister::updatePortableRegistration(
      settings, portablePath, /*registered=*/false);
  ASSERT_TRUE(result) << qPrintable(result.error);
  QSettings removed(settingsPath, QSettings::IniFormat);
  EXPECT_TRUE(removed.value("PortableInstances").toStringList().isEmpty());
}

TEST(InstanceUnregister, PortableRegistryReplaceCommitsAsOneGeneration)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString settingsPath = temporary.filePath("global.ini");
  const QString oldPath = temporary.filePath("old-portable");
  const QString newPath = temporary.filePath("new-portable");
  QSettings settings(settingsPath, QSettings::IniFormat);
  settings.setValue("PortableInstances",
                    QStringList{QFileInfo(oldPath).absoluteFilePath(),
                                temporary.filePath("other")});
  settings.sync();

  const auto result = InstanceUnregister::replacePortableRegistration(
      settings, oldPath, newPath);
  ASSERT_TRUE(result) << qPrintable(result.error);
  QSettings persisted(settingsPath, QSettings::IniFormat);
  EXPECT_EQ(persisted.value("PortableInstances").toStringList(),
            (QStringList{temporary.filePath("other"),
                         QFileInfo(newPath).absoluteFilePath()}));
}

#ifdef Q_OS_LINUX
TEST(InstanceUnregister, PortableRegistryCollapsesPhysicalAliases)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString settingsPath = temporary.filePath("global.ini");
  const QString portablePath = temporary.filePath("portable");
  const QString aliasPath = temporary.filePath("portable-alias");
  ASSERT_TRUE(QDir().mkdir(portablePath));
  ASSERT_EQ(::symlink(QFile::encodeName(portablePath).constData(),
                      QFile::encodeName(aliasPath).constData()),
            0);

  QSettings settings(settingsPath, QSettings::IniFormat);
  settings.setValue("PortableInstances", QStringList{portablePath, aliasPath});
  settings.sync();
  ASSERT_EQ(settings.status(), QSettings::NoError);

  auto result = InstanceUnregister::updatePortableRegistration(
      settings, aliasPath, /*registered=*/true);
  ASSERT_TRUE(result) << qPrintable(result.error);
  QSettings deduplicated(settingsPath, QSettings::IniFormat);
  EXPECT_EQ(deduplicated.value("PortableInstances").toStringList(),
            QStringList{QFileInfo(aliasPath).absoluteFilePath()});

  result = InstanceUnregister::updatePortableRegistration(
      settings, portablePath, /*registered=*/false);
  ASSERT_TRUE(result) << qPrintable(result.error);
  QSettings removed(settingsPath, QSettings::IniFormat);
  EXPECT_TRUE(removed.value("PortableInstances").toStringList().isEmpty());
}
#endif

TEST(InstanceUnregister, FailedWriteCannotPublishFromBackendDestruction)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString settingsPath = temporary.filePath("global.registry-test");
  const auto format = failureInjectingFormat();
  ASSERT_NE(format, QSettings::InvalidFormat);
  const QString original = temporary.filePath("original");
  {
    QSettings seed(settingsPath, format);
    seed.setValue("PortableInstances", QStringList{original});
    seed.sync();
    ASSERT_EQ(seed.status(), QSettings::NoError);
  }

  auto backend = std::make_unique<QSettings>(settingsPath, format);
  rejectCustomSettingsWrites.store(true);
  const auto result = InstanceUnregister::updatePortableRegistration(
      *backend, temporary.filePath("rejected"), /*registered=*/true);
  EXPECT_EQ(result.status, InstanceUnregister::RegistryStatus::RollbackFailed);
  EXPECT_FALSE(result.error.isEmpty());
  (void)backend.release();

  rejectCustomSettingsWrites.store(false);
  QSettings reopened(settingsPath, format);
  EXPECT_EQ(reopened.value("PortableInstances").toStringList(),
            QStringList{original});
}

#ifdef Q_OS_LINUX
TEST(InstanceUnregister, ConcurrentProcessesSerializeTheWholeRegistryMutation)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString settingsPath = temporary.filePath("global.ini");
  const QString first = temporary.filePath("first");
  const QString second = temporary.filePath("second");
  {
    QSettings seed(settingsPath, QSettings::IniFormat);
    seed.setValue("PortableInstances", QStringList{first, second});
    seed.sync();
    ASSERT_EQ(seed.status(), QSettings::NoError);
  }

  int firstReady[2];
  int releaseFirst[2];
  int secondReady[2];
  ASSERT_EQ(::pipe(firstReady), 0);
  ASSERT_EQ(::pipe(releaseFirst), 0);
  ASSERT_EQ(::pipe(secondReady), 0);

  const pid_t firstChild = ::fork();
  ASSERT_GE(firstChild, 0);
  if (firstChild == 0) {
    ::close(firstReady[0]);
    ::close(releaseFirst[1]);
    ::close(secondReady[0]);
    ::close(secondReady[1]);
    QSettings settings(settingsPath, QSettings::IniFormat);
    const auto result = InstanceUnregister::updatePortableRegistration(
        settings, first, /*registered=*/false, [&] {
          const char ready = '1';
          if (::write(firstReady[1], &ready, 1) != 1) {
            ::_exit(20);
          }
          char release = 0;
          if (::read(releaseFirst[0], &release, 1) != 1) {
            ::_exit(21);
          }
        });
    ::_exit(result ? 0 : 10);
  }

  ::close(firstReady[1]);
  ::close(releaseFirst[0]);
  char ready = 0;
  ASSERT_EQ(::read(firstReady[0], &ready, 1), 1);

  const pid_t secondChild = ::fork();
  ASSERT_GE(secondChild, 0);
  if (secondChild == 0) {
    ::close(secondReady[0]);
    ::close(releaseFirst[1]);
    ::close(firstReady[0]);
    QSettings settings(settingsPath, QSettings::IniFormat);
    const auto result = InstanceUnregister::updatePortableRegistration(
        settings, second, /*registered=*/false, [&] {
          const char acquired = '2';
          if (::write(secondReady[1], &acquired, 1) != 1) {
            ::_exit(22);
          }
        });
    ::_exit(result ? 0 : 11);
  }
  ::close(secondReady[1]);

  struct pollfd blocked{secondReady[0], POLLIN, 0};
  EXPECT_EQ(::poll(&blocked, 1, 200), 0)
      << "the second process entered the read/modify/write transaction early";
  const char release = 'x';
  ASSERT_EQ(::write(releaseFirst[1], &release, 1), 1);

  int firstStatus = 0;
  ASSERT_EQ(::waitpid(firstChild, &firstStatus, 0), firstChild);
  ASSERT_TRUE(WIFEXITED(firstStatus));
  ASSERT_EQ(WEXITSTATUS(firstStatus), 0);
  ASSERT_EQ(::read(secondReady[0], &ready, 1), 1);
  int secondStatus = 0;
  ASSERT_EQ(::waitpid(secondChild, &secondStatus, 0), secondChild);
  ASSERT_TRUE(WIFEXITED(secondStatus));
  ASSERT_EQ(WEXITSTATUS(secondStatus), 0);

  QSettings result(settingsPath, QSettings::IniFormat);
  EXPECT_TRUE(result.value("PortableInstances").toStringList().isEmpty());
  ::close(firstReady[0]);
  ::close(releaseFirst[1]);
  ::close(secondReady[0]);
}
#endif

#include "audioruntime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
QByteArray read(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}

void write(const QString& path, const QByteArray& data)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(data), data.size());
}

const QByteArray kGuard = "MZ guard forwarding to x3daudio1_7_ms.X3DAudioCalculate "
                          "x3daudio1_7_ms.X3DAudioInitialize";
const QByteArray kMicrosoft = "MZ microsoft X3DAudioCalculate X3DAudioInitialize";
const QByteArray kWine = "MZ  Wine builtin DLL X3DAudioCalculate X3DAudioInitialize";

class AudioGuardTest : public testing::Test {
protected:
  QTemporaryDir temporary;
  QString prefix = temporary.path() + "/pfx";
  QString bundle = temporary.path() + "/audio-guard";
  QString system32 = prefix + "/drive_c/windows/system32";
  bool changed = false;
  QString skipped, error;

  void SetUp() override
  {
    ASSERT_TRUE(temporary.isValid());
    ASSERT_TRUE(QDir().mkpath(system32));
    write(bundle + "/x3daudio1_7.dll", kGuard);
    write(bundle + "/x3daudio1_7.dll.sha256",
          QCryptographicHash::hash(kGuard, QCryptographicHash::Sha256).toHex() +
              "  x3daudio1_7.dll\n");
  }

  bool install() { return installAudioGuard(prefix, bundle, changed, skipped, error); }

  QStringList files() const
  {
    return QDir(system32).entryList(QDir::Files | QDir::System, QDir::Name);
  }
};

TEST_F(AudioGuardTest, WrapsMicrosoftDllInstalledByDxsetup)
{
  write(system32 + "/X3DAudio1_7.dll", kMicrosoft);
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_TRUE(changed);
  EXPECT_TRUE(skipped.isEmpty());
  EXPECT_EQ(files(), QStringList({"x3daudio1_7.dll", "x3daudio1_7_ms.dll"}));
  EXPECT_EQ(read(system32 + "/x3daudio1_7.dll"), kGuard);
  EXPECT_EQ(read(system32 + "/x3daudio1_7_ms.dll"), kMicrosoft);

  ASSERT_TRUE(install());
  EXPECT_FALSE(changed) << "a second launch must not touch the prefix";
}

TEST_F(AudioGuardTest, RewrapsAfterDxsetupReplacesTheGuard)
{
  write(system32 + "/x3daudio1_7_ms.dll", "MZ old X3DAudioCalculate X3DAudioInitialize");
  write(system32 + "/X3DAudio1_7.dll", kMicrosoft);
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_TRUE(changed);
  EXPECT_EQ(files(), QStringList({"x3daudio1_7.dll", "x3daudio1_7_ms.dll"}));
  EXPECT_EQ(read(system32 + "/x3daudio1_7_ms.dll"), kMicrosoft);
}

TEST_F(AudioGuardTest, RestoresMissingGuardBesideMicrosoftDll)
{
  write(system32 + "/x3daudio1_7_ms.dll", kMicrosoft);
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_TRUE(changed);
  EXPECT_EQ(read(system32 + "/x3daudio1_7.dll"), kGuard);
}

TEST_F(AudioGuardTest, LeavesWineBuiltinAlone)
{
  write(system32 + "/x3daudio1_7.dll", kWine);
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_FALSE(changed);
  EXPECT_FALSE(skipped.isEmpty());
  EXPECT_EQ(files(), QStringList({"x3daudio1_7.dll"}));
  EXPECT_EQ(read(system32 + "/x3daudio1_7.dll"), kWine);
}

TEST_F(AudioGuardTest, RemovesGuardWhoseTargetIsMissing)
{
  write(system32 + "/x3daudio1_7.dll", kGuard);
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_TRUE(changed);
  EXPECT_FALSE(skipped.isEmpty());
  EXPECT_TRUE(files().isEmpty()) << "games must fall back to Wine's builtin X3DAudio";
}

TEST_F(AudioGuardTest, RejectsDamagedBundle)
{
  write(bundle + "/x3daudio1_7.dll", "MZ tampered");
  write(system32 + "/X3DAudio1_7.dll", kMicrosoft);
  EXPECT_FALSE(install());
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(files(), QStringList({"X3DAudio1_7.dll"}));
}

TEST_F(AudioGuardTest, RefusesLinkedSystemDirectory)
{
  QDir(system32).removeRecursively();
  const QString elsewhere = temporary.path() + "/shared-runner";
  ASSERT_TRUE(QDir().mkpath(elsewhere));
  write(elsewhere + "/X3DAudio1_7.dll", kMicrosoft);
  ASSERT_TRUE(QFile::link(elsewhere, system32));
  EXPECT_FALSE(install());
  EXPECT_EQ(read(elsewhere + "/X3DAudio1_7.dll"), kMicrosoft);
}

TEST(AudioRuntime, DetectsBundledFAudioPrefixes)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  EXPECT_FALSE(prefixUsesBundledFAudio(temporary.path()));
  write(temporary.path() + "/.fluorine-faudio/version.txt", "faudio=26.04\n");
  EXPECT_TRUE(prefixUsesBundledFAudio(temporary.path()));
}
}  // namespace

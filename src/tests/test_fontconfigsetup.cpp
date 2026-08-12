#include "fontconfigsetup.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace
{
class EnvironmentValue
{
public:
  explicit EnvironmentValue(const char* name)
      : m_Name(name), m_WasSet(qEnvironmentVariableIsSet(name)),
        m_Value(qgetenv(name))
  {}

  ~EnvironmentValue()
  {
    if (m_WasSet) {
      qputenv(m_Name, m_Value);
    } else {
      qunsetenv(m_Name);
    }
  }

private:
  const char* m_Name;
  bool m_WasSet;
  QByteArray m_Value;
};

class FontconfigSetupTest : public testing::Test
{
protected:
  FontconfigSetupTest()
      : m_Disable("FLUORINE_DISABLE_FONTCONFIG_FIX"),
        m_File("FONTCONFIG_FILE"), m_Path("FONTCONFIG_PATH"),
        m_OriginalFile("FLUORINE_ORIG_FONTCONFIG_FILE"),
        m_OriginalPath("FLUORINE_ORIG_FONTCONFIG_PATH")
  {
    qunsetenv("FLUORINE_DISABLE_FONTCONFIG_FIX");
    qunsetenv("FONTCONFIG_FILE");
    qunsetenv("FONTCONFIG_PATH");
    qunsetenv("FLUORINE_ORIG_FONTCONFIG_FILE");
    qunsetenv("FLUORINE_ORIG_FONTCONFIG_PATH");
  }

  EnvironmentValue m_Disable;
  EnvironmentValue m_File;
  EnvironmentValue m_Path;
  EnvironmentValue m_OriginalFile;
  EnvironmentValue m_OriginalPath;
};
}

TEST_F(FontconfigSetupTest, ApplicationDirectoryIgnoresPathBasenameAndCwd)
{
  const QString runningExecutable =
      QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
  ASSERT_FALSE(runningExecutable.isEmpty());
  const QString expected = QFileInfo(runningExecutable).absolutePath();

  QTemporaryDir unrelatedCwd;
  ASSERT_TRUE(unrelatedCwd.isValid());
  const QString originalCwd = QDir::currentPath();
  ASSERT_TRUE(QDir::setCurrent(unrelatedCwd.path()));

  const QString resolved = FontconfigSetup::applicationDirectory(
      QStringLiteral("test_fontconfigsetup"));
  const bool restored = QDir::setCurrent(originalCwd);

  EXPECT_TRUE(restored);
  EXPECT_EQ(resolved, expected);
  EXPECT_NE(resolved, unrelatedCwd.path());
}

TEST_F(FontconfigSetupTest, UsesExistingPackagedConfigWithoutRewritingIt)
{
  QTemporaryDir app;
  ASSERT_TRUE(app.isValid());
  const QString fontDir = QDir(app.path()).filePath(QStringLiteral("etc/fonts"));
  ASSERT_TRUE(QDir().mkpath(fontDir));
  const QString configPath = QDir(fontDir).filePath(QStringLiteral("fonts.conf"));

  QFile config(configPath);
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  ASSERT_EQ(config.write("packaged-config"), 15);
  config.close();
  ASSERT_TRUE(config.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadGroup |
                                    QFileDevice::ReadOther));

  const auto result = FontconfigSetup::configure(app.path());

  EXPECT_EQ(result.state, FontconfigSetup::State::Active);
  EXPECT_FALSE(result.generated);
  EXPECT_EQ(result.configPath, configPath);
  EXPECT_EQ(qgetenv("FONTCONFIG_FILE"), QFile::encodeName(configPath));
  EXPECT_EQ(qgetenv("FONTCONFIG_PATH"), QFile::encodeName(fontDir));
  ASSERT_TRUE(config.open(QIODevice::ReadOnly));
  EXPECT_EQ(config.readAll(), QByteArray("packaged-config"));
}

TEST_F(FontconfigSetupTest, GeneratesEmbeddedFallbackInWritableBuildTree)
{
  QTemporaryDir app;
  ASSERT_TRUE(app.isValid());

  const auto result = FontconfigSetup::configure(app.path());

  ASSERT_EQ(result.state, FontconfigSetup::State::Active);
  EXPECT_TRUE(result.generated);
  QFile config(result.configPath);
  ASSERT_TRUE(config.open(QIODevice::ReadOnly));
  const QByteArray contents = config.readAll();
  EXPECT_TRUE(contents.contains("prefix=\"relative\""));
  EXPECT_TRUE(contents.contains("../../fonts"));
  EXPECT_FALSE(contents.contains("/etc/fonts"));
  EXPECT_FALSE(contents.contains("/usr/share/fontconfig"));
}

TEST_F(FontconfigSetupTest, ExistingNonDirectoryFailsClosedToHostConfig)
{
  QTemporaryDir app;
  ASSERT_TRUE(app.isValid());
  qputenv("FONTCONFIG_FILE", "/caller/fonts.conf");
  qputenv("FONTCONFIG_PATH", "/caller/fonts");
  QFile blocker(QDir(app.path()).filePath(QStringLiteral("etc")));
  ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
  blocker.close();

  const auto result = FontconfigSetup::configure(app.path());

  EXPECT_EQ(result.state, FontconfigSetup::State::Unavailable);
  EXPECT_FALSE(result.generated);
  EXPECT_EQ(qgetenv("FONTCONFIG_FILE"), QByteArray("/caller/fonts.conf"));
  EXPECT_EQ(qgetenv("FONTCONFIG_PATH"), QByteArray("/caller/fonts"));
  EXPECT_FALSE(qEnvironmentVariableIsSet("FLUORINE_ORIG_FONTCONFIG_FILE"));
  EXPECT_FALSE(qEnvironmentVariableIsSet("FLUORINE_ORIG_FONTCONFIG_PATH"));
}

TEST_F(FontconfigSetupTest, EscapeHatchPreservesCallerEnvironment)
{
  qputenv("FLUORINE_DISABLE_FONTCONFIG_FIX", "1");
  qputenv("FONTCONFIG_FILE", "/caller/fonts.conf");
  qputenv("FONTCONFIG_PATH", "/caller/fonts");

  const auto result = FontconfigSetup::configure(QStringLiteral("/unused"));

  EXPECT_EQ(result.state, FontconfigSetup::State::Disabled);
  EXPECT_EQ(qgetenv("FONTCONFIG_FILE"), QByteArray("/caller/fonts.conf"));
  EXPECT_EQ(qgetenv("FONTCONFIG_PATH"), QByteArray("/caller/fonts"));
  EXPECT_FALSE(qEnvironmentVariableIsSet("FLUORINE_ORIG_FONTCONFIG_FILE"));
  EXPECT_FALSE(qEnvironmentVariableIsSet("FLUORINE_ORIG_FONTCONFIG_PATH"));
}

TEST_F(FontconfigSetupTest, ExistingOriginalSnapshotIsNotOverwritten)
{
  QTemporaryDir app;
  ASSERT_TRUE(app.isValid());
  qputenv("FONTCONFIG_FILE", "/nested/fonts.conf");
  qputenv("FONTCONFIG_PATH", "/nested/fonts");
  qputenv("FLUORINE_ORIG_FONTCONFIG_FILE", "/caller/fonts.conf");
  qputenv("FLUORINE_ORIG_FONTCONFIG_PATH", "/caller/fonts");

  ASSERT_EQ(FontconfigSetup::configure(app.path()).state,
            FontconfigSetup::State::Active);

  EXPECT_EQ(qgetenv("FLUORINE_ORIG_FONTCONFIG_FILE"),
            QByteArray("/caller/fonts.conf"));
  EXPECT_EQ(qgetenv("FLUORINE_ORIG_FONTCONFIG_PATH"),
            QByteArray("/caller/fonts"));
}

TEST_F(FontconfigSetupTest, ChildEnvironmentRestoresCallerConfiguration)
{
  QTemporaryDir app;
  ASSERT_TRUE(app.isValid());
  qputenv("FONTCONFIG_FILE", "/caller/fonts.conf");
  qputenv("FONTCONFIG_PATH", "/caller/fonts");

  ASSERT_EQ(FontconfigSetup::configure(app.path()).state,
            FontconfigSetup::State::Active);
  QProcessEnvironment child = QProcessEnvironment::systemEnvironment();
  FontconfigSetup::restoreCallerEnvironment(child);

  EXPECT_EQ(child.value(QStringLiteral("FONTCONFIG_FILE")),
            QStringLiteral("/caller/fonts.conf"));
  EXPECT_EQ(child.value(QStringLiteral("FONTCONFIG_PATH")),
            QStringLiteral("/caller/fonts"));
  EXPECT_FALSE(child.contains(QStringLiteral("FLUORINE_ORIG_FONTCONFIG_FILE")));
  EXPECT_FALSE(child.contains(QStringLiteral("FLUORINE_ORIG_FONTCONFIG_PATH")));
}

TEST_F(FontconfigSetupTest, ChildEnvironmentStripsPrivateConfigWhenCallerHadNone)
{
  QTemporaryDir app;
  ASSERT_TRUE(app.isValid());

  ASSERT_EQ(FontconfigSetup::configure(app.path()).state,
            FontconfigSetup::State::Active);
  QProcessEnvironment child = QProcessEnvironment::systemEnvironment();
  FontconfigSetup::restoreCallerEnvironment(child);

  EXPECT_FALSE(child.contains(QStringLiteral("FONTCONFIG_FILE")));
  EXPECT_FALSE(child.contains(QStringLiteral("FONTCONFIG_PATH")));
  EXPECT_FALSE(child.contains(QStringLiteral("FLUORINE_ORIG_FONTCONFIG_FILE")));
  EXPECT_FALSE(child.contains(QStringLiteral("FLUORINE_ORIG_FONTCONFIG_PATH")));
}

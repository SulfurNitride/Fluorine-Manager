#include "protonsettingsedit.h"
#include "restarttransaction.h"
#include "settingsmigration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace
{
class ScopedConfigHome
{
public:
  explicit ScopedConfigHome(const QString& path)
      : m_Previous(qgetenv("XDG_CONFIG_HOME"))
  {
    EXPECT_TRUE(qputenv("XDG_CONFIG_HOME", path.toUtf8()));
  }

  ~ScopedConfigHome()
  {
    if (m_Previous.isNull()) {
      qunsetenv("XDG_CONFIG_HOME");
    } else {
      qputenv("XDG_CONFIG_HOME", m_Previous);
    }
  }

private:
  QByteArray m_Previous;
};

FluorineConfig initialConfig(const QString& prefix = QStringLiteral("/prefix/old"))
{
  FluorineConfig config;
  config.app_id      = 42;
  config.prefix_path = prefix;
  config.proton_name = QStringLiteral("Proton Old");
  config.proton_path = QStringLiteral("/proton/old");
  config.created     = QStringLiteral("2026-01-01T00:00:00");
  return config;
}

void seedSettings(QSettings& settings, bool includeKeys = true)
{
  if (includeKeys) {
    settings.setValue(ProtonSettingsEdit::LaunchWrapperKey,
                      QStringLiteral("old-wrapper"));
    settings.setValue(ProtonSettingsEdit::DisableVfsCacheKey, false);
  }
  settings.setValue(QStringLiteral("unrelated/key"), QStringLiteral("keep"));
  settings.sync();
  ASSERT_EQ(settings.status(), QSettings::NoError);
}

void expectOldValues(QSettings& settings)
{
  settings.sync();
  EXPECT_EQ(settings.value(ProtonSettingsEdit::LaunchWrapperKey).toString(),
            QStringLiteral("old-wrapper"));
  EXPECT_FALSE(
      settings.value(ProtonSettingsEdit::DisableVfsCacheKey).toBool());
  EXPECT_EQ(settings.value(QStringLiteral("unrelated/key")).toString(),
            QStringLiteral("keep"));

  const auto config = FluorineConfig::load();
  ASSERT_TRUE(config);
  EXPECT_EQ(config->proton_name, QStringLiteral("Proton Old"));
  EXPECT_EQ(config->proton_path, QStringLiteral("/proton/old"));
}
}  // namespace

TEST(ProtonSettingsEdit, CancelRestoresLaunchCacheAndProtonSelection)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ScopedConfigHome configHome(temporary.filePath(QStringLiteral("config")));
  QSettings settings(temporary.filePath(QStringLiteral("default.ini")),
                     QSettings::IniFormat);
  seedSettings(settings);
  ASSERT_TRUE(initialConfig().save());

  const auto original = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(original);
  ASSERT_TRUE(ProtonSettingsEdit::persist(
      settings, QStringLiteral("new-wrapper"), true,
      QStringLiteral("Proton New"), QStringLiteral("/proton/new")));
  const auto rejected = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(rejected);

  ASSERT_TRUE(ProtonSettingsEdit::restore(settings, *original, *rejected));
  EXPECT_TRUE(ProtonSettingsEdit::verify(settings, *original, *rejected));
  expectOldValues(settings);
}

TEST(ProtonSettingsEdit, RefusedRestartRestoresAbsentKeysAndProtonSelection)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ScopedConfigHome configHome(temporary.filePath(QStringLiteral("config")));
  QSettings settings(temporary.filePath(QStringLiteral("default.ini")),
                     QSettings::IniFormat);
  seedSettings(settings, false);
  ASSERT_TRUE(initialConfig().save());

  const auto original = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(original);
  ASSERT_TRUE(ProtonSettingsEdit::persist(
      settings, QStringLiteral("new-wrapper"), true,
      QStringLiteral("Proton New"), QStringLiteral("/proton/new")));
  const auto rejected = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(rejected);

  ASSERT_TRUE(ProtonSettingsEdit::restore(settings, *original, *rejected));
  ASSERT_TRUE(ProtonSettingsEdit::verify(settings, *original, *rejected));
  settings.sync();
  EXPECT_FALSE(settings.contains(ProtonSettingsEdit::LaunchWrapperKey));
  EXPECT_FALSE(settings.contains(ProtonSettingsEdit::DisableVfsCacheKey));
  const auto config = FluorineConfig::load();
  ASSERT_TRUE(config);
  EXPECT_EQ(config->proton_name, QStringLiteral("Proton Old"));
  EXPECT_EQ(config->proton_path, QStringLiteral("/proton/old"));
}

TEST(ProtonSettingsEdit, AcceptedUpdateRetainsCommittedValues)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ScopedConfigHome configHome(temporary.filePath(QStringLiteral("config")));
  QSettings settings(temporary.filePath(QStringLiteral("default.ini")),
                     QSettings::IniFormat);
  seedSettings(settings);
  ASSERT_TRUE(initialConfig().save());

  ASSERT_TRUE(ProtonSettingsEdit::persist(
      settings, QStringLiteral("new-wrapper"), true,
      QStringLiteral("Proton New"), QStringLiteral("/proton/new")));

  settings.sync();
  EXPECT_EQ(settings.value(ProtonSettingsEdit::LaunchWrapperKey).toString(),
            QStringLiteral("new-wrapper"));
  EXPECT_TRUE(settings.value(ProtonSettingsEdit::DisableVfsCacheKey).toBool());
  const auto config = FluorineConfig::load();
  ASSERT_TRUE(config);
  EXPECT_EQ(config->proton_name, QStringLiteral("Proton New"));
  EXPECT_EQ(config->proton_path, QStringLiteral("/proton/new"));
}

TEST(ProtonSettingsEdit, ExplicitPrefixReplacementIsOutsideRollbackScope)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ScopedConfigHome configHome(temporary.filePath(QStringLiteral("config")));
  QSettings settings(temporary.filePath(QStringLiteral("default.ini")),
                     QSettings::IniFormat);
  seedSettings(settings);
  ASSERT_TRUE(initialConfig().save());
  const auto original = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(original);

  auto replacement = initialConfig(QStringLiteral("/prefix/replaced"));
  replacement.created = QStringLiteral("2026-08-11T10:00:00");
  replacement.proton_name = QStringLiteral("Proton Replacement");
  replacement.proton_path = QStringLiteral("/proton/replacement");
  ASSERT_TRUE(replacement.save());
  ASSERT_TRUE(ProtonSettingsEdit::persist(
      settings, QStringLiteral("new-wrapper"), true,
      replacement.proton_name, replacement.proton_path));
  const auto rejected = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(rejected);

  ASSERT_TRUE(ProtonSettingsEdit::restore(settings, *original, *rejected));
  ASSERT_TRUE(ProtonSettingsEdit::verify(settings, *original, *rejected));
  const auto retained = FluorineConfig::load();
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained->prefix_path, replacement.prefix_path);
  EXPECT_EQ(retained->created, replacement.created);
  EXPECT_EQ(retained->proton_name, replacement.proton_name);
  EXPECT_EQ(retained->proton_path, replacement.proton_path);
  settings.sync();
  EXPECT_EQ(settings.value(ProtonSettingsEdit::LaunchWrapperKey).toString(),
            QStringLiteral("old-wrapper"));
  EXPECT_FALSE(
      settings.value(ProtonSettingsEdit::DisableVfsCacheKey).toBool());
}

TEST(ProtonSettingsEdit, RollbackLockFailureIsPersistenceFailure)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ScopedConfigHome configHome(temporary.filePath(QStringLiteral("config")));
  QSettings settings(temporary.filePath(QStringLiteral("default.ini")),
                     QSettings::IniFormat);
  seedSettings(settings);
  ASSERT_TRUE(initialConfig().save());
  const auto original = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(original);
  ASSERT_TRUE(ProtonSettingsEdit::persist(
      settings, QStringLiteral("new-wrapper"), true,
      QStringLiteral("Proton New"), QStringLiteral("/proton/new")));
  const auto rejected = ProtonSettingsEdit::capture(settings);
  ASSERT_TRUE(rejected);

  QLockFile competing(
      SettingsMigration::settingsLockPath(settings.fileName()));
  competing.setStaleLockTime(SettingsMigration::SettingsLockStaleMs);
  ASSERT_TRUE(competing.tryLock());
  const auto result = restart_transaction::restoreAfterRefusal(
      [&] { return ProtonSettingsEdit::restore(settings, *original, *rejected, 0); },
      [] { return true; }, [] { return true; }, [] { return true; });
  EXPECT_EQ(result, restart_transaction::RollbackResult::PersistenceFailed);
}

TEST(ProtonSettingsEdit, ExistingMalformedConfigFailsPreflightCapture)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ScopedConfigHome configHome(temporary.filePath(QStringLiteral("config")));
  QSettings settings(temporary.filePath(QStringLiteral("default.ini")),
                     QSettings::IniFormat);
  seedSettings(settings);

  const QString configPath = FluorineConfig::configFilePath();
  ASSERT_TRUE(QDir().mkpath(QFileInfo(configPath).dir().absolutePath()));
  QFile malformed(configPath);
  ASSERT_TRUE(malformed.open(QIODevice::WriteOnly));
  ASSERT_EQ(malformed.write("not-json"), 8);
  malformed.close();

  EXPECT_FALSE(ProtonSettingsEdit::capture(settings));
}

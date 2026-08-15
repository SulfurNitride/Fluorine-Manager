#include "protondxvkconfig.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace
{
constexpr auto Desired = "dxvk.enableGraphicsPipelineLibrary = False\n";

void writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
  file.close();
  ASSERT_EQ(file.error(), QFileDevice::NoError);
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}
}  // namespace

TEST(ProtonDxvkConfig, PublishesOwnedConfigWithoutTouchingUserConfig)
{
  QTemporaryDir prefix;
  ASSERT_TRUE(prefix.isValid());

  const QString userConfig = QDir(prefix.path()).filePath("dxvk.conf");
  writeFile(userConfig, "dxvk.hud = full\n");

  const auto result = ProtonDxvkConfig::publish(prefix.path());
  ASSERT_TRUE(result) << result.error.toStdString();
  EXPECT_EQ(QFileInfo(result.path).fileName(), ".fluorine-dxvk.conf");
  EXPECT_EQ(readFile(result.path), Desired);
  EXPECT_EQ(readFile(userConfig), "dxvk.hud = full\n");
}

TEST(ProtonDxvkConfig, AcceptsAnIdenticalExistingGeneration)
{
  QTemporaryDir prefix;
  ASSERT_TRUE(prefix.isValid());

  const QString generated = QDir(prefix.path()).filePath(".fluorine-dxvk.conf");
  writeFile(generated, Desired);
  const auto beforeMtime =
      QFileInfo(generated).fileTime(QFileDevice::FileModificationTime);

  const auto result = ProtonDxvkConfig::publish(prefix.path());
  ASSERT_TRUE(result) << result.error.toStdString();
  EXPECT_EQ(readFile(generated), Desired);
  EXPECT_EQ(QFileInfo(generated).fileTime(QFileDevice::FileModificationTime),
            beforeMtime);
}

TEST(ProtonDxvkConfig, PreservesAConflictingGeneratedPath)
{
  QTemporaryDir prefix;
  ASSERT_TRUE(prefix.isValid());

  const QString collision = QDir(prefix.path()).filePath(".fluorine-dxvk.conf");
  writeFile(collision, "user-owned = true\n");

  const auto result = ProtonDxvkConfig::publish(prefix.path());
  EXPECT_FALSE(result);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_EQ(readFile(collision), "user-owned = true\n");
}

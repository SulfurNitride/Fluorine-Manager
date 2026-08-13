#include "portablelauncherscript.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <sys/stat.h>

using namespace portable_launcher_script;

namespace
{

bool writeExecutable(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
    return false;
  }
  file.close();
  return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                             QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                             QFileDevice::ExeGroup | QFileDevice::ReadOther |
                             QFileDevice::ExeOther);
}

QString makeToolPath(const QTemporaryDir& temp)
{
  const QString path = temp.filePath(QStringLiteral("tools"));
  if (!QDir().mkpath(path)) {
    return {};
  }

  for (const QString& tool : {QStringLiteral("bash"), QStringLiteral("readlink"),
                              QStringLiteral("dirname")}) {
    const QString source = QStandardPaths::findExecutable(tool);
    if (source.isEmpty() || !QFile::link(source, QDir(path).filePath(tool))) {
      return {};
    }
  }
  return path;
}

QProcessEnvironment environment(const QString& path, const QString& home,
                                const QString& capture)
{
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("PATH"), path);
  env.insert(QStringLiteral("HOME"), home);
  env.insert(QStringLiteral("CAPTURE_PATH"), capture);
  return env;
}

QByteArray captureScript(const QByteArray& marker = {})
{
  return QByteArrayLiteral("#!/usr/bin/env bash\n") +
         QByteArrayLiteral("printf '") + marker +
         QByteArrayLiteral("%s\\0' \"$@\" > \"$CAPTURE_PATH\"\n");
}

QList<QByteArray> capturedArguments(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  auto result = file.readAll().split('\0');
  if (!result.isEmpty() && result.back().isEmpty()) {
    result.pop_back();
  }
  return result;
}

}

TEST(PortableLauncherScript, CreatesAtomicExecutableLauncher)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString instance = temp.filePath(QStringLiteral("portable"));
  ASSERT_TRUE(QDir().mkpath(instance));

  const Result result = create(instance);
  ASSERT_EQ(result.status, Status::Created) << result.error.toStdString();

  QFile file(result.path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QByteArray contents = file.readAll();
  EXPECT_TRUE(contents.startsWith("#!/usr/bin/env bash\nset -euo pipefail\n"));
  EXPECT_TRUE(contents.contains("type -P fluorine-manager"));
  EXPECT_TRUE(contents.contains("--instance \"$INSTANCE_DIR\" \"$@\""));
  EXPECT_FALSE(contents.contains("ModOrganizer-core"));
  EXPECT_FALSE(contents.contains("which ModOrganizer"));

  struct stat st;
  ASSERT_EQ(::stat(result.path.toLocal8Bit().constData(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0755);
}

TEST(PortableLauncherScript, PreservesExistingEntries)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());

  const QString regularDir = temp.filePath(QStringLiteral("regular"));
  ASSERT_TRUE(QDir().mkpath(regularDir));
  const QString regularPath = QDir(regularDir).filePath("ModOrganizer.sh");
  ASSERT_TRUE(writeExecutable(regularPath, QByteArrayLiteral("custom\n")));
  const auto originalPermissions = QFile::permissions(regularPath);
  EXPECT_EQ(create(regularDir).status, Status::Preserved);
  QFile regular(regularPath);
  ASSERT_TRUE(regular.open(QIODevice::ReadOnly));
  EXPECT_EQ(regular.readAll(), QByteArrayLiteral("custom\n"));
  EXPECT_EQ(QFile::permissions(regularPath), originalPermissions);

  const QString linkDir = temp.filePath(QStringLiteral("link"));
  ASSERT_TRUE(QDir().mkpath(linkDir));
  const QString linkPath = QDir(linkDir).filePath("ModOrganizer.sh");
  const QString missingTarget = temp.filePath(QStringLiteral("missing-target"));
  ASSERT_TRUE(QFile::link(missingTarget, linkPath));
  EXPECT_EQ(create(linkDir).status, Status::Preserved);
  EXPECT_TRUE(QFileInfo(linkPath).isSymLink());
  EXPECT_EQ(QFileInfo(linkPath).symLinkTarget(), missingTarget);

  const QString directoryDir = temp.filePath(QStringLiteral("directory"));
  const QString directoryPath = QDir(directoryDir).filePath("ModOrganizer.sh");
  ASSERT_TRUE(QDir().mkpath(directoryPath));
  EXPECT_EQ(create(directoryDir).status, Status::Preserved);
  EXPECT_TRUE(QFileInfo(directoryPath).isDir());
}

TEST(PortableLauncherScript, ReportsCreationFailureWithoutArtifact)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString invalidParent = temp.filePath(QStringLiteral("not-a-directory"));
  QFile file(invalidParent);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.close();

  const Result result = create(invalidParent);
  EXPECT_EQ(result.status, Status::Failed);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_FALSE(QFileInfo::exists(result.path));
}

TEST(PortableLauncherScript, UsesPathLauncherAndPreservesArguments)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString tools = makeToolPath(temp);
  ASSERT_FALSE(tools.isEmpty());

  const QString instance =
      temp.filePath(QStringLiteral("instance with spaces '$;"));
  ASSERT_TRUE(QDir().mkpath(instance));
  const Result result = create(instance);
  ASSERT_EQ(result.status, Status::Created) << result.error.toStdString();

  const QString capture = temp.filePath(QStringLiteral("path-capture"));
  ASSERT_TRUE(writeExecutable(QDir(tools).filePath("fluorine-manager"),
                              captureScript()));

  const QString home = temp.filePath(QStringLiteral("home"));
  const QString fallback =
      QDir(home).filePath(".local/share/fluorine/bin/fluorine-manager");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(fallback).absolutePath()));
  ASSERT_TRUE(writeExecutable(fallback, captureScript("fallback:")));

  const QString externalLink = temp.filePath(QStringLiteral("launch link"));
  ASSERT_TRUE(QFile::link(result.path, externalLink));

  QProcess process;
  process.setProcessEnvironment(environment(tools, home, capture));
  process.start(externalLink,
                {QStringLiteral(""), QStringLiteral("two words"),
                 QStringLiteral("$HOME;*.esp'\"")});
  ASSERT_TRUE(process.waitForFinished(10000));
  EXPECT_EQ(process.exitStatus(), QProcess::NormalExit);
  EXPECT_EQ(process.exitCode(), 0);

  EXPECT_EQ(capturedArguments(capture),
            (QList<QByteArray>{"--instance", QDir(instance).canonicalPath().toUtf8(),
                               "", "two words", "$HOME;*.esp'\""}));
}

TEST(PortableLauncherScript, UsesStableHomeFallback)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString tools = makeToolPath(temp);
  ASSERT_FALSE(tools.isEmpty());
  const QString instance = temp.filePath(QStringLiteral("instance"));
  ASSERT_TRUE(QDir().mkpath(instance));
  const Result result = create(instance);
  ASSERT_EQ(result.status, Status::Created);

  const QString home = temp.filePath(QStringLiteral("home"));
  const QString fallback =
      QDir(home).filePath(".local/share/fluorine/bin/fluorine-manager");
  ASSERT_TRUE(QDir().mkpath(QFileInfo(fallback).absolutePath()));
  const QString capture = temp.filePath(QStringLiteral("fallback-capture"));
  ASSERT_TRUE(writeExecutable(fallback, captureScript()));

  QProcess process;
  process.setProcessEnvironment(environment(tools, home, capture));
  process.start(result.path, {QStringLiteral("argument")});
  ASSERT_TRUE(process.waitForFinished(10000));
  EXPECT_EQ(process.exitCode(), 0);
  EXPECT_EQ(capturedArguments(capture),
            (QList<QByteArray>{"--instance", QDir(instance).canonicalPath().toUtf8(),
                               "argument"}));
}

TEST(PortableLauncherScript, MissingManagerIsActionable)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString tools = makeToolPath(temp);
  ASSERT_FALSE(tools.isEmpty());
  const QString instance = temp.filePath(QStringLiteral("instance"));
  ASSERT_TRUE(QDir().mkpath(instance));
  const Result result = create(instance);
  ASSERT_EQ(result.status, Status::Created);

  QProcess process;
  process.setProcessEnvironment(environment(
      tools, temp.filePath(QStringLiteral("empty-home")),
      temp.filePath(QStringLiteral("unused-capture"))));
  process.start(result.path);
  ASSERT_TRUE(process.waitForFinished(10000));
  EXPECT_EQ(process.exitCode(), 127);
  EXPECT_TRUE(process.readAllStandardError().contains(
      "fluorine-manager launcher was not found"));
}

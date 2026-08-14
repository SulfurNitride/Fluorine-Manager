#include "nxmhandlerintegration.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>

namespace
{
using namespace nxm_handler_integration;

bool writeFile(const QString& path, const QByteArray& bytes)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
         file.flush();
}

bool writeExecutable(const QString& path, const QByteArray& bytes)
{
  return writeFile(path, bytes) &&
         QFile::setPermissions(path,
                               QFileDevice::ReadOwner |
                                   QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner |
                                   QFileDevice::ReadGroup |
                                   QFileDevice::ExeGroup |
                                   QFileDevice::ReadOther |
                                   QFileDevice::ExeOther);
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

Paths testPaths(const QString& root)
{
  Paths paths;
  paths.desktop = root + "/xdg-data/applications/" +
                  QString::fromLatin1(CurrentDesktopFile);
  paths.historicalDesktop = root + "/home/.local/share/applications/" +
                            QString::fromLatin1(CurrentDesktopFile);
  paths.legacyDesktop = root + "/home/.local/share/applications/" +
                        QString::fromLatin1(LegacyDesktopFile);
  paths.legacyWrapper = root + "/home/.local/bin/mo2-nxm-handler";
  paths.mimeApps      = root + "/xdg-config/mimeapps.list";
  paths.legacyMimeApps = {
      root + "/home/.config/mimeapps.list",
      root + "/home/.local/share/applications/mimeapps.list",
  };
  paths.lockFile = root + "/xdg-config/fluorine-nxm-handler.lock";
  QDir().mkpath(QFileInfo(paths.lockFile).absolutePath());
  return paths;
}

const QByteArray ExistingMime =
    "# user comment\n"
    "[Default Applications]\n"
    "x-scheme-handler/nxm=other.desktop;fallback.desktop;\n"
    "x-scheme-handler/modl=other.desktop;\n"
    "text/plain=editor.desktop;\n"
    "\n"
    "[Added Associations]\n"
    "x-scheme-handler/nxm=other.desktop;\n";

const QByteArray LegacyWrapper =
    "#!/bin/sh\nexec \"/opt/ModOrganizer\" nxm-handle \"$@\"\n";

TEST(NxmHandlerIntegration, RendersDesktopExecWithFreedesktopEscaping)
{
  QString error;
  const QString rendered = desktopEntry(
      QStringLiteral("/tmp/O'Brien \\$`\"%%/fluorine-manager"), &error);
  ASSERT_TRUE(error.isEmpty());
  EXPECT_TRUE(rendered.contains(QStringLiteral("X-Fluorine-Managed=nxm-handler-v1")));
  EXPECT_TRUE(rendered.contains(QStringLiteral(" nxm-handle %u")));
  EXPECT_TRUE(rendered.contains(QStringLiteral("\\\\")));
  EXPECT_TRUE(rendered.contains(QStringLiteral("\\$")));
  EXPECT_TRUE(rendered.contains(QStringLiteral("\\`")));
  EXPECT_TRUE(rendered.contains(QStringLiteral("\\\"")));
  EXPECT_TRUE(rendered.contains(QStringLiteral("%%%%")));

  EXPECT_TRUE(desktopEntry(QStringLiteral("bad\npath"), &error).isEmpty());
  EXPECT_FALSE(error.isEmpty());
}

TEST(NxmHandlerIntegration, InstallIsAtomicIdempotentAndPreservesFallbacks)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const Paths paths = testPaths(temp.path());
  ASSERT_TRUE(writeFile(paths.mimeApps, ExistingMime));

  const auto first = install(paths, QStringLiteral("/opt/Fluorine Manager"), true);
  ASSERT_TRUE(first.succeeded()) << first.message.toStdString();
  EXPECT_TRUE(first.changed);
  const QByteArray desktop = readFile(paths.desktop);
  EXPECT_NE(desktop.indexOf("X-Fluorine-Managed=nxm-handler-v1"), -1);
  EXPECT_FALSE(QFileInfo::exists(paths.legacyWrapper));

  const QByteArray mime = readFile(paths.mimeApps);
  EXPECT_NE(mime.indexOf("# user comment"), -1);
  EXPECT_NE(mime.indexOf("text/plain=editor.desktop;"), -1);
  EXPECT_NE(mime.indexOf(
                "x-scheme-handler/nxm=com.fluorine.manager.nxm-handler.desktop;other.desktop;fallback.desktop;"),
            -1);
  EXPECT_NE(mime.indexOf(
                "x-scheme-handler/modl=com.fluorine.manager.nxm-handler.desktop;other.desktop;"),
            -1);

  const auto second = install(paths, QStringLiteral("/opt/Fluorine Manager"), true);
  EXPECT_EQ(second.status, Status::NoChange);
  EXPECT_FALSE(second.changed);
  EXPECT_EQ(readFile(paths.mimeApps), mime);
}

TEST(NxmHandlerIntegration, PassiveRefreshDoesNotStealExternalDefault)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  ASSERT_TRUE(writeFile(paths.mimeApps, ExistingMime));

  const auto result =
      install(paths, QStringLiteral("/opt/fluorine-manager"), false);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  const QByteArray mime = readFile(paths.mimeApps);
  EXPECT_NE(mime.indexOf(
                "x-scheme-handler/nxm=other.desktop;fallback.desktop;com.fluorine.manager.nxm-handler.desktop;"),
            -1);
  EXPECT_NE(mime.indexOf(
                "x-scheme-handler/modl=other.desktop;com.fluorine.manager.nxm-handler.desktop;"),
            -1);
}

TEST(NxmHandlerIntegration, ForeignDesktopCollisionLeavesMimeUntouched)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  ASSERT_TRUE(writeFile(paths.desktop, "unowned\n"));
  ASSERT_TRUE(writeFile(paths.mimeApps, ExistingMime));

  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  EXPECT_EQ(result.status, Status::Collision);
  EXPECT_EQ(readFile(paths.desktop), QByteArray("unowned\n"));
  EXPECT_EQ(readFile(paths.mimeApps), ExistingMime);
}

TEST(NxmHandlerIntegration, SymlinkCollisionDoesNotTouchVictim)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QString victim = temp.path() + "/victim";
  ASSERT_TRUE(writeFile(victim, "important"));
  QDir().mkpath(QFileInfo(paths.desktop).absolutePath());
  ASSERT_TRUE(QFile::link(victim, paths.desktop));

  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  EXPECT_EQ(result.status, Status::Collision);
  EXPECT_EQ(readFile(victim), QByteArray("important"));
  EXPECT_TRUE(QFileInfo(paths.desktop).isSymLink());
}

TEST(NxmHandlerIntegration, InvalidMimeUtf8IsPreserved)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  QByteArray invalid("[Default Applications]\n");
  invalid.append(char(0xff));
  invalid.append(char(0xfe));
  ASSERT_TRUE(writeFile(paths.mimeApps, invalid));

  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  EXPECT_EQ(result.status, Status::Collision);
  EXPECT_EQ(readFile(paths.mimeApps), invalid);
  EXPECT_FALSE(QFileInfo::exists(paths.desktop));
}

TEST(NxmHandlerIntegration, RecognizedLegacyArtifactsConvergeForward)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray legacyDesktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Mod Organizer 2 NXM Handler\n"
                     "Exec=%1 nxm-handle %u\n"
                     "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  ASSERT_TRUE(writeFile(paths.legacyDesktop, legacyDesktop));
  ASSERT_TRUE(writeExecutable(paths.legacyWrapper, LegacyWrapper));
  ASSERT_TRUE(writeFile(
      paths.mimeApps,
      "[Default Applications]\n"
      "x-scheme-handler/nxm=mo2-nxm-handler.desktop;other.desktop;\n"
      "x-scheme-handler/modl=mo2-nxm-handler.desktop;\n"
      "[Added Associations]\n"
      "x-scheme-handler/nxm=mo2-nxm-handler.desktop;\n"
      "x-scheme-handler/modl=mo2-nxm-handler.desktop;\n"));
  EXPECT_TRUE(recognizesCompleteRegistration(paths));

  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), false);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_FALSE(QFileInfo::exists(paths.legacyDesktop));
  EXPECT_FALSE(QFileInfo::exists(paths.legacyWrapper));
  EXPECT_TRUE(QFileInfo::exists(paths.desktop));
  const QByteArray mime = readFile(paths.mimeApps);
  EXPECT_EQ(mime.indexOf("mo2-nxm-handler.desktop"), -1);
  EXPECT_NE(mime.indexOf("com.fluorine.manager.nxm-handler.desktop"), -1);
}

TEST(NxmHandlerIntegration, UninstallScrubsOnlyOwnedIdsAndCreatesNothing)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  EXPECT_EQ(uninstall(paths).status, Status::NoChange);
  EXPECT_FALSE(QFileInfo::exists(paths.mimeApps));

  ASSERT_TRUE(install(paths, QStringLiteral("/opt/fluorine-manager"), true)
                  .succeeded());
  const QByteArray ownedDesktop = readFile(paths.desktop);
  QByteArray mime = readFile(paths.mimeApps);
  mime += "[Removed Associations]\n"
          "x-scheme-handler/nxm=foreign.desktop;com.fluorine.manager.desktop;\n";
  ASSERT_TRUE(writeFile(paths.mimeApps, mime));

  const auto result = uninstall(paths);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_FALSE(QFileInfo::exists(paths.desktop));
  const QByteArray after = readFile(paths.mimeApps);
  EXPECT_EQ(after.count("com.fluorine.manager.nxm-handler.desktop"), 2);
  EXPECT_NE(after.indexOf(
                "x-scheme-handler/nxm=com.fluorine.manager.nxm-handler.desktop;foreign.desktop;com.fluorine.manager.desktop;"),
            -1);
  EXPECT_NE(after.indexOf(
                "x-scheme-handler/modl=com.fluorine.manager.nxm-handler.desktop;"),
            -1);
  EXPECT_NE(after.indexOf("foreign.desktop"), -1);
  EXPECT_NE(after.indexOf("com.fluorine.manager.desktop"), -1);

  // Model a crash or late unlink failure after the MIME commit: even if the
  // exact owned desktop survives, Removed Associations prevents adoption.
  ASSERT_TRUE(writeFile(paths.desktop, ownedDesktop));
  EXPECT_FALSE(recognizesCompleteRegistration(paths));
  ASSERT_TRUE(QFile::remove(paths.desktop));
  EXPECT_EQ(uninstall(paths).status, Status::NoChange);
}

TEST(NxmHandlerIntegration, UninstallBlacklistsOwnedDesktopWithoutMimeEntry)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  for (const bool foreignOnlyMime : {false, true}) {
    SCOPED_TRACE(foreignOnlyMime);
    const Paths paths =
        testPaths(temp.path() + (foreignOnlyMime ? "/foreign" : "/missing"));
    QString error;
    const QByteArray ownedDesktop =
        desktopEntry(QStringLiteral("/opt/fluorine-manager"), &error).toUtf8();
    ASSERT_TRUE(error.isEmpty());
    ASSERT_TRUE(writeFile(paths.desktop, ownedDesktop));
    if (foreignOnlyMime) {
      ASSERT_TRUE(writeFile(paths.mimeApps,
                            "[Default Applications]\n"
                            "text/plain=editor.desktop;\n"));
    }

    const auto result = uninstall(paths);
    ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
    EXPECT_FALSE(QFileInfo::exists(paths.desktop));
    const QByteArray mime = readFile(paths.mimeApps);
    EXPECT_EQ(mime.count("com.fluorine.manager.nxm-handler.desktop"), 2);
    EXPECT_NE(mime.indexOf("[Removed Associations]"), -1);
    if (foreignOnlyMime) {
      EXPECT_NE(mime.indexOf("text/plain=editor.desktop;"), -1);
    }

    ASSERT_TRUE(writeFile(paths.desktop, ownedDesktop));
    EXPECT_FALSE(recognizesCompleteRegistration(paths));
    ASSERT_TRUE(QFile::remove(paths.desktop));
  }
}

TEST(NxmHandlerIntegration, InstallCleansOnlyOwnedLegacyBlacklists)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray legacyDesktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Mod Organizer 2 NXM Handler\n"
                     "Exec=%1 nxm-handle %u\n"
                     "MimeType=x-scheme-handler/nxm;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  ASSERT_TRUE(writeFile(paths.legacyDesktop, legacyDesktop));
  ASSERT_TRUE(writeExecutable(paths.legacyWrapper, LegacyWrapper));
  ASSERT_TRUE(writeFile(
      paths.legacyMimeApps.constFirst(),
      "[Removed Associations]\n"
      "x-scheme-handler/nxm=com.fluorine.manager.nxm-handler.desktop;mo2-nxm-handler.desktop;com.fluorine.manager.desktop;foreign.desktop;\n"
      "x-scheme-handler/modl=com.fluorine.manager.nxm-handler.desktop;mo2-nxm-handler.desktop;foreign.desktop;\n"));

  const auto result =
      install(paths, QStringLiteral("/opt/fluorine-manager"), false);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  const QByteArray legacyMime = readFile(paths.legacyMimeApps.constFirst());
  EXPECT_EQ(legacyMime.indexOf("com.fluorine.manager.nxm-handler.desktop"), -1);
  EXPECT_EQ(legacyMime.indexOf("mo2-nxm-handler.desktop"), -1);
  EXPECT_NE(legacyMime.indexOf("com.fluorine.manager.desktop"), -1);
  EXPECT_NE(legacyMime.indexOf("foreign.desktop"), -1);
}

TEST(NxmHandlerIntegration, ExistingLockBoundsConcurrentMutation)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  QLockFile held(paths.lockFile);
  ASSERT_TRUE(held.tryLock());
  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  EXPECT_EQ(result.status, Status::Busy);
  EXPECT_FALSE(QFileInfo::exists(paths.desktop));
}

TEST(NxmHandlerIntegration, RemovedOnlyRegistrationIsNotAdopted)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  QString error;
  ASSERT_TRUE(writeFile(paths.desktop,
                        desktopEntry(QStringLiteral("/opt/fluorine-manager"),
                                     &error)
                            .toUtf8()));
  ASSERT_TRUE(error.isEmpty());
  ASSERT_TRUE(writeFile(
      paths.mimeApps,
      "[Removed Associations]\n"
      "x-scheme-handler/nxm=com.fluorine.manager.nxm-handler.desktop;\n"
      "x-scheme-handler/modl=com.fluorine.manager.nxm-handler.desktop;\n"));
  EXPECT_FALSE(recognizesCompleteRegistration(paths));
}

TEST(NxmHandlerIntegration, ForeignHistoricalArtifactsArePreserved)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray otherWrapper =
      "#!/bin/sh\nexec \"/opt/OtherModOrganizer\" nxm-handle \"$@\"\n";
  const QByteArray otherDesktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Mod Organizer 2 NXM Handler\n"
                     "Exec=%1 nxm-handle %u\n"
                     "MimeType=x-scheme-handler/nxm;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  ASSERT_TRUE(writeFile(paths.legacyWrapper, otherWrapper));
  ASSERT_TRUE(writeFile(paths.legacyDesktop, otherDesktop));

  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_EQ(readFile(paths.legacyWrapper), otherWrapper);
  EXPECT_EQ(readFile(paths.legacyDesktop), otherDesktop);
  EXPECT_TRUE(QFileInfo::exists(paths.desktop));

  ASSERT_TRUE(uninstall(paths).succeeded());
  EXPECT_EQ(readFile(paths.legacyWrapper), otherWrapper);
  EXPECT_EQ(readFile(paths.legacyDesktop), otherDesktop);
}

TEST(NxmHandlerIntegration, RenamedAppImageLegacyWrapperIsRecognized)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray legacyDesktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Mod Organizer 2 NXM Handler\n"
                     "Exec=%1 nxm-handle %u\n"
                     "MimeType=x-scheme-handler/nxm;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  const QByteArray wrapper =
      "#!/bin/sh\n"
      "url=$1\n"
      "[ -n \"$url\" ] || exit 2\n"
      "if \"/opt/My Organizer.AppImage\" nxm-handle \"$url\"; then\n"
      "  exit 0\n"
      "fi\n"
      "exec \"/opt/My Organizer.AppImage\" \"$url\"\n";
  ASSERT_TRUE(writeFile(paths.legacyDesktop, legacyDesktop));
  ASSERT_TRUE(writeExecutable(paths.legacyWrapper, wrapper));
  ASSERT_TRUE(writeFile(
      paths.mimeApps,
      "[Default Applications]\n"
      "x-scheme-handler/nxm=mo2-nxm-handler.desktop;\n"));

  EXPECT_TRUE(recognizesCompleteRegistration(paths));
  const auto result =
      install(paths, QStringLiteral("/opt/fluorine-manager"), false);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_FALSE(QFileInfo::exists(paths.legacyDesktop));
  EXPECT_FALSE(QFileInfo::exists(paths.legacyWrapper));
}

TEST(NxmHandlerIntegration, MismatchedHandoffWrapperIsPreserved)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray legacyDesktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Mod Organizer 2 NXM Handler\n"
                     "Exec=%1 nxm-handle %u\n"
                     "MimeType=x-scheme-handler/nxm;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  const QByteArray wrapper =
      "#!/bin/sh\n"
      "url=$1\n"
      "[ -n \"$url\" ] || exit 2\n"
      "if \"/opt/One.AppImage\" nxm-handle \"$url\"; then\n"
      "  exit 0\n"
      "fi\n"
      "exec \"/opt/Two.AppImage\" \"$url\"\n";
  ASSERT_TRUE(writeFile(paths.legacyDesktop, legacyDesktop));
  ASSERT_TRUE(writeExecutable(paths.legacyWrapper, wrapper));
  ASSERT_TRUE(writeFile(
      paths.mimeApps,
      "[Default Applications]\n"
      "x-scheme-handler/nxm=mo2-nxm-handler.desktop;\n"));

  EXPECT_FALSE(recognizesCompleteRegistration(paths));
  const auto result =
      install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_EQ(readFile(paths.legacyDesktop), legacyDesktop);
  EXPECT_EQ(readFile(paths.legacyWrapper), wrapper);
}

TEST(NxmHandlerIntegration, UnpairedHistoricalCurrentDesktopIsPreserved)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray desktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Fluorine Manager NXM Handler\n"
                     "Exec=%1 %u\n"
                     "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  ASSERT_TRUE(writeFile(paths.historicalDesktop, desktop));
  ASSERT_TRUE(writeFile(
      paths.mimeApps,
      "[Default Applications]\n"
      "x-scheme-handler/nxm=com.fluorine.manager.nxm-handler.desktop;\n"
      "x-scheme-handler/modl=com.fluorine.manager.nxm-handler.desktop;\n"));

  EXPECT_FALSE(recognizesCompleteRegistration(paths));
  const auto result =
      install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_EQ(readFile(paths.historicalDesktop), desktop);
}

TEST(NxmHandlerIntegration, UnpairedHistoricalDesktopAtCurrentTargetCollides)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray desktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Fluorine Manager NXM Handler\n"
                     "Exec=%1 %u\n"
                     "MimeType=x-scheme-handler/nxm;x-scheme-handler/modl;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  ASSERT_TRUE(writeFile(paths.desktop, desktop));

  const auto result =
      install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  EXPECT_EQ(result.status, Status::Collision);
  EXPECT_EQ(readFile(paths.desktop), desktop);
}

TEST(NxmHandlerIntegration, NxmOnlyLegacyRegistrationIsAdoptedFromOldConfig)
{
  QTemporaryDir temp;
  const Paths paths = testPaths(temp.path());
  const QByteArray legacyDesktop =
      QStringLiteral("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Mod Organizer 2 NXM Handler\n"
                     "Exec=%1 nxm-handle %u\n"
                     "MimeType=x-scheme-handler/nxm;\n"
                     "NoDisplay=true\n")
          .arg(paths.legacyWrapper)
          .toUtf8();
  ASSERT_TRUE(writeFile(paths.legacyDesktop, legacyDesktop));
  ASSERT_TRUE(writeExecutable(paths.legacyWrapper, LegacyWrapper));
  ASSERT_TRUE(writeFile(
      paths.legacyMimeApps.constFirst(),
      "[Default Applications]\n"
      "x-scheme-handler/nxm=mo2-nxm-handler.desktop;\n"));
  EXPECT_TRUE(recognizesCompleteRegistration(paths));
}

TEST(NxmHandlerIntegration, CanonicalAliasesDoNotUndoCurrentPublication)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString realRoot = temp.path() + "/real";
  const QString alias    = temp.path() + "/alias";
  ASSERT_TRUE(QDir().mkpath(realRoot + "/applications"));
  ASSERT_TRUE(QDir().mkpath(realRoot + "/config"));
  ASSERT_TRUE(QFile::link(realRoot, alias));

  Paths paths;
  paths.desktop = realRoot + "/applications/" +
                  QString::fromLatin1(CurrentDesktopFile);
  paths.historicalDesktop = alias + "/applications/" +
                            QString::fromLatin1(CurrentDesktopFile);
  paths.legacyDesktop = temp.path() + "/legacy/" +
                        QString::fromLatin1(LegacyDesktopFile);
  paths.legacyWrapper = temp.path() + "/bin/mo2-nxm-handler";
  paths.mimeApps      = realRoot + "/config/mimeapps.list";
  paths.legacyMimeApps = {alias + "/config/mimeapps.list"};
  paths.lockFile = realRoot + "/config/handler.lock";

  ASSERT_TRUE(install(paths, QStringLiteral("/opt/fluorine-manager"), true)
                  .succeeded());
  EXPECT_TRUE(QFileInfo::exists(paths.desktop));
  EXPECT_NE(readFile(paths.mimeApps)
                .indexOf("com.fluorine.manager.nxm-handler.desktop"),
            -1);
  ASSERT_TRUE(install(paths, QStringLiteral("/opt/fluorine-manager"), false)
                  .succeeded());
  EXPECT_TRUE(QFileInfo::exists(paths.desktop));
}

TEST(NxmHandlerIntegration, OwnerControlledSymlinkedParentIsSupported)
{
  QTemporaryDir temp;
  const QString realData = temp.path() + "/real-data";
  const QString dataLink = temp.path() + "/data-link";
  ASSERT_TRUE(QDir().mkpath(realData + "/applications"));
  ASSERT_TRUE(QFile::link(realData, dataLink));
  Paths paths = testPaths(temp.path());
  paths.desktop = dataLink + "/applications/" +
                  QString::fromLatin1(CurrentDesktopFile);
  paths.historicalDesktop.clear();

  const auto result = install(paths, QStringLiteral("/opt/fluorine-manager"), true);
  ASSERT_TRUE(result.succeeded()) << result.message.toStdString();
  EXPECT_TRUE(QFileInfo::exists(realData + "/applications/" +
                                QString::fromLatin1(CurrentDesktopFile)));
}

}  // namespace

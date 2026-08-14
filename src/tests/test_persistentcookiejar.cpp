#include "persistentcookiejar.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QNetworkCookie>
#include <QTemporaryDir>
#include <QUrl>

#include <uibase/log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace
{
class TestCookieJar : public PersistentCookieJar
{
public:
  using PersistentCookieJar::PersistentCookieJar;

  QList<QNetworkCookie> cookies() const { return allCookies(); }
};

QNetworkCookie cookie(QByteArray name, QByteArray value, bool persistent)
{
  QNetworkCookie result(std::move(name), std::move(value));
  result.setDomain(QStringLiteral("example.test"));
  result.setPath(QStringLiteral("/mods"));
  result.setSecure(true);
  result.setHttpOnly(true);
  if (persistent) {
    result.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(7));
  }
  return result;
}

void addCookie(TestCookieJar& jar, const QNetworkCookie& value)
{
  ASSERT_TRUE(
      jar.setCookiesFromUrl({value}, QUrl(QStringLiteral("https://example.test/mods"))));
}

void writeBytes(const QString& path, const QByteArray& bytes)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
  file.close();
}

QByteArray legacyJar(const QList<QNetworkCookie>& cookies)
{
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << static_cast<quint32>(cookies.size());
  for (const auto& value : cookies) {
    stream << value.toRawForm();
  }
  EXPECT_EQ(stream.status(), QDataStream::Ok);
  return bytes;
}

bool containsCookie(const QList<QNetworkCookie>& cookies, QByteArrayView name)
{
  return std::any_of(cookies.cbegin(), cookies.cend(),
                     [name](const auto& value) { return value.name() == name; });
}
}  // namespace

TEST(PersistentCookieJarTest, PersistsOnlyLivePersistentCookies)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("cookies.dat"));

  {
    TestCookieJar jar(path);
    addCookie(jar, cookie("persistent", "secret", true));
    addCookie(jar, cookie("session", "temporary", false));
  }

  TestCookieJar restored(path);
  const auto cookies = restored.cookies();
  ASSERT_EQ(cookies.size(), 1);
  EXPECT_TRUE(containsCookie(cookies, "persistent"));
  EXPECT_FALSE(containsCookie(cookies, "session"));
  EXPECT_EQ(cookies.front().path(), QStringLiteral("/mods"));
  EXPECT_TRUE(cookies.front().isSecure());
  EXPECT_TRUE(cookies.front().isHttpOnly());
}

TEST(PersistentCookieJarTest, RestoresLegacyPersistentButNotSessionCookies)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path     = temp.filePath(QStringLiteral("cookies.dat"));
  QNetworkCookie expired = cookie("expired", "discarded", true);
  expired.setExpirationDate(QDateTime::currentDateTimeUtc().addDays(-1));
  writeBytes(path, legacyJar({cookie("persistent", "saved", true),
                              cookie("session", "discarded", false), expired}));

  TestCookieJar restored(path);
  const auto cookies = restored.cookies();
  ASSERT_EQ(cookies.size(), 1);
  EXPECT_TRUE(containsCookie(cookies, "persistent"));
  EXPECT_FALSE(containsCookie(cookies, "session"));
  EXPECT_FALSE(containsCookie(cookies, "expired"));
}

TEST(PersistentCookieJarTest, RejectsTruncatedAndHostileLegacyLengthsWithoutPartialState)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QNetworkCookie valid = cookie("persistent", "saved", true);

  QList<QByteArray> malformed;
  {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << quint32{0xffffffff};
    malformed.append(bytes);
  }
  {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << quint32{1} << quint32{16 * 1024 * 1024};
    malformed.append(bytes);
  }
  {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << quint32{2} << valid.toRawForm() << quint32{100};
    malformed.append(bytes);
  }
  malformed.append(legacyJar({valid}) + QByteArray("trailing"));

  for (qsizetype i = 0; i < malformed.size(); ++i) {
    const QString path = temp.filePath(QStringLiteral("malformed-%1.dat").arg(i));
    writeBytes(path, malformed[i]);
    TestCookieJar jar(path);
    EXPECT_TRUE(jar.cookies().isEmpty()) << "fixture " << i;
  }
}

TEST(PersistentCookieJarTest, ClearIsDurableBeforeDestruction)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("cookies.dat"));

  {
    TestCookieJar jar(path);
    addCookie(jar, cookie("persistent", "secret", true));
  }

  TestCookieJar jar(path);
  ASSERT_EQ(jar.cookies().size(), 1);
  jar.clear();

  TestCookieJar restored(path);
  EXPECT_TRUE(restored.cookies().isEmpty());
}

TEST(PersistentCookieJarTest, PublishedJarIsOwnerOnly)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("cookies.dat"));
  writeBytes(path, legacyJar({cookie("persistent", "secret", true)}));
  const auto permissive = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                          QFileDevice::ReadGroup | QFileDevice::ReadOther;
  ASSERT_TRUE(QFile::setPermissions(path, permissive));

  {
    TestCookieJar jar(path);
  }

  const QFileInfo info(path);
  ASSERT_TRUE(info.isFile());
  const auto permissions = info.permissions();
  EXPECT_TRUE(permissions.testFlag(QFileDevice::ReadOwner));
  EXPECT_TRUE(permissions.testFlag(QFileDevice::WriteOwner));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::ReadGroup));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::WriteGroup));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::ExeGroup));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::ReadOther));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::WriteOther));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::ExeOther));
}

#ifdef Q_OS_UNIX
TEST(PersistentCookieJarTest, RejectsFifoWithoutBlocking)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString path = temp.filePath(QStringLiteral("cookies.dat"));
  ASSERT_EQ(::mkfifo(QFile::encodeName(path).constData(), 0600), 0)
      << std::strerror(errno);

  {
    TestCookieJar jar(path);
    EXPECT_TRUE(jar.cookies().isEmpty());
  }

  const QFileInfo replacement(path);
  EXPECT_TRUE(replacement.exists());
  EXPECT_FALSE(replacement.isFile());
  EXPECT_FALSE(replacement.isSymLink());
}

TEST(PersistentCookieJarTest, DoesNotFollowCookieFileSymlink)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString target = temp.filePath(QStringLiteral("external.dat"));
  const QString path   = temp.filePath(QStringLiteral("cookies.dat"));
  const QByteArray original = legacyJar({cookie("external", "secret", true)});
  writeBytes(target, original);
  ASSERT_TRUE(QFile::link(target, path));

  {
    TestCookieJar jar(path);
    EXPECT_TRUE(jar.cookies().isEmpty());
  }

  QFile targetFile(target);
  ASSERT_TRUE(targetFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(targetFile.readAll(), original);
  const QFileInfo replacement(path);
  EXPECT_TRUE(replacement.isSymLink());
}
#endif

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  MOBase::log::LoggerConfiguration logging;
  logging.name     = "test_persistentcookiejar";
  logging.maxLevel = MOBase::log::Warning;
  MOBase::log::createDefault(std::move(logging));
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

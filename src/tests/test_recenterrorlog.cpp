#include "recenterrorlog.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace
{

QString writeLog(QTemporaryDir& dir, const QByteArray& contents)
{
  const QString path = dir.filePath(QStringLiteral("mo_interface_test.log"));
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::WriteOnly));
  EXPECT_EQ(file.write(contents), contents.size());
  file.close();
  return path;
}

}  // namespace

TEST(RecentErrorLog, FindsCurrentFormatAndEscapesContext)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = writeLog(
      dir, "[2026-08-14 12:00:00.000Z I] before <private>\n"
           "[2026-08-14 12:00:01.000Z E] failed & unsafe\n"
           "[2026-08-14 12:00:02.000Z I] after\n");

  const auto result = diagnose_basic::recentErrorContext(path);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->contains(QStringLiteral("&lt;private&gt;")));
  EXPECT_TRUE(result->contains(
      QStringLiteral("<b>[2026-08-14 12:00:01.000Z E] failed &amp; unsafe</b>")));
  EXPECT_TRUE(result->contains(QStringLiteral("<br>")));
}

TEST(RecentErrorLog, UsesMostRecentErrorAndAcceptsLegacyFormat)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = writeLog(
      dir, "ERROR: first\nline 1\nline 2\nline 3\nline 4\nline 5\nline 6\n"
           "ERROR second\nlast\n");

  const auto result = diagnose_basic::recentErrorContext(path);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->contains(QStringLiteral("<b>ERROR second</b>")));
  EXPECT_FALSE(result->contains(QStringLiteral("<b>ERROR: first</b>")));
}

TEST(RecentErrorLog, IgnoresTextThatOnlyMentionsError)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = writeLog(
      dir, "[2026-08-14 12:00:00.000Z I] text says ERROR but is informational\n"
           "[2026-08-14 12:00:01.000Z W] warning\n");

  EXPECT_FALSE(diagnose_basic::recentErrorContext(path).has_value());
  EXPECT_FALSE(
      diagnose_basic::recentErrorContext(dir.filePath(QStringLiteral("missing.log")))
          .has_value());
}

TEST(RecentErrorLog, ScansBoundedTail)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QByteArray contents(4 * 1024 * 1024 + 1024, 'x');
  contents.append("\n[2026-08-14 12:00:01.000Z E] recent tail failure\n");
  const QString path = writeLog(dir, contents);

  const auto result = diagnose_basic::recentErrorContext(path);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->contains(QStringLiteral("recent tail failure")));
}

TEST(RecentErrorLog, ReachesEndAfterMoreThanFiftyThousandShortLines)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QByteArray contents;
  contents.reserve(2 * 1024 * 1024);
  for (int i = 0; i < 60000; ++i) {
    contents.append("[2026-08-14 I] ordinary line\n");
  }
  contents.append("[2026-08-14 E] final short-line failure\n");
  const QString path = writeLog(dir, contents);

  const auto result = diagnose_basic::recentErrorContext(path);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->contains(QStringLiteral("final short-line failure")));
}

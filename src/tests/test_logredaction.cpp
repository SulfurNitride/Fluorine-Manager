#include <uibase/log.h>

#include <gtest/gtest.h>

using MOBase::log::safeUrlForLog;

TEST(LogRedaction, RemovesHttpCapabilities)
{
  const QString summary = safeUrlForLog(QStringLiteral(
      "HTTPS://user:password@example.com/SECRET_PATH?token=SECRET_QUERY"
      "#SECRET_FRAGMENT"));

  EXPECT_EQ(summary, QStringLiteral("https://example.com"));
  EXPECT_FALSE(summary.contains(QStringLiteral("user")));
  EXPECT_FALSE(summary.contains(QStringLiteral("password")));
  EXPECT_FALSE(summary.contains(QStringLiteral("SECRET")));
}

TEST(LogRedaction, RetainsRepositoryIdentityWithoutCredentials)
{
  const QString summary = safeUrlForLog(QStringLiteral(
      "nxm://skyrimspecialedition/mods/12/files/34?key=SECRET_KEY&expires=99"
      "&user_id=7#SECRET_FRAGMENT"));

  EXPECT_EQ(summary,
            QStringLiteral("nxm://skyrimspecialedition/mods/12/files/34"));
  EXPECT_FALSE(summary.contains(QStringLiteral("SECRET")));
  EXPECT_FALSE(summary.contains(QStringLiteral("user_id")));
}

TEST(LogRedaction, HidesNestedDirectDownload)
{
  const QString summary = safeUrlForLog(QStringLiteral(
      "modl://game?url=https%3A%2F%2Fcdn.example%2FSECRET_PATH%3Fsig%3DSECRET"));

  EXPECT_EQ(summary, QStringLiteral("modl://game"));
  EXPECT_FALSE(summary.contains(QStringLiteral("SECRET")));
}

TEST(LogRedaction, NeverRetainsModlOrEncodedPaths)
{
  const QString modl = safeUrlForLog(QStringLiteral(
      "MODL://game/mods/12/files/34?url=https%3A%2F%2Fcdn.example%2FSECRET"));
  const QString encoded = safeUrlForLog(
      QStringLiteral("NXM://game/private/%53ECRET_PATH?key=SECRET_QUERY"));

  EXPECT_EQ(modl, QStringLiteral("modl://game/<redacted>"));
  EXPECT_EQ(encoded, QStringLiteral("nxm://game/<redacted>"));
  EXPECT_FALSE(modl.contains(QStringLiteral("SECRET")));
  EXPECT_FALSE(encoded.contains(QStringLiteral("SECRET")));
}

TEST(LogRedaction, HidesUnrecognizedRepositoryPaths)
{
  const QString summary = safeUrlForLog(
      QStringLiteral("nxm://game/private/SECRET_PATH?key=SECRET_QUERY"));
  const QString overflow = safeUrlForLog(
      QStringLiteral("nxm://game/mods/999999999999/files/34?key=SECRET_QUERY"));

  EXPECT_EQ(summary, QStringLiteral("nxm://game/<redacted>"));
  EXPECT_EQ(overflow, QStringLiteral("nxm://game/<redacted>"));
  EXPECT_FALSE(summary.contains(QStringLiteral("SECRET")));
  EXPECT_FALSE(overflow.contains(QStringLiteral("SECRET")));
}

TEST(LogRedaction, InvalidAndUnsupportedInputFailsClosed)
{
  const QString malformed = safeUrlForLog(
      QStringLiteral("https://bad host/SECRET_MALFORMED\r\nInjected"));
  const QString unsupported =
      safeUrlForLog(QStringLiteral("file:///home/user/SECRET_FILE"));

  EXPECT_EQ(malformed, QStringLiteral("<redacted URL>"));
  EXPECT_EQ(unsupported, QStringLiteral("<redacted URL>"));
  EXPECT_FALSE(malformed.contains(QStringLiteral("SECRET")));
  EXPECT_FALSE(unsupported.contains(QStringLiteral("SECRET")));
}

#pragma warning(push)
#pragma warning(disable : 4668)
#include <gtest/gtest.h>
#pragma warning(pop)

#include <QString>
#include <vector>

#include <uibase/versioning.h>

#include <format>

using namespace MOBase;

using enum Version::ReleaseType;
using ParseMode = Version::ParseMode;

TEST(VersioningTest, VersionParse)
{
  // TODO: add exceptions test

  // semver
  ASSERT_EQ(Version(1, 0, 0), Version::parse("1.0.0"));
  ASSERT_EQ(Version(1, 0, 0, Development, 1), Version::parse("1.0.0-dev.1"));
  ASSERT_EQ(Version(1, 0, 0, Development, 2), Version::parse("1.0.0-dev.2"));
  ASSERT_EQ(Version(1, 0, 0, Alpha), Version::parse("1.0.0-a"));
  ASSERT_EQ(Version(1, 0, 0, Alpha), Version::parse("1.0.0-alpha"));
  ASSERT_EQ(Version(1, 0, 0, 0, {Alpha, 1, Beta}), Version::parse("1.0.0-alpha.1.b"));
  ASSERT_EQ(Version(1, 0, 0, Beta, 2), Version::parse("1.0.0-beta.2"));
  ASSERT_EQ(Version(2, 5, 2, ReleaseCandidate, 1), Version::parse("2.5.2-rc.1"));

  // mo2
  ASSERT_EQ(Version(1, 0, 0), Version::parse("1.0.0", ParseMode::MO2));
  ASSERT_EQ(Version(1, 0, 0, Development, 1),
            Version::parse("1.0.0dev1", ParseMode::MO2));
  ASSERT_EQ(Version(1, 0, 0, Development, 2),
            Version::parse("1.0.0dev2", ParseMode::MO2));
  ASSERT_EQ(Version(1, 0, 0, Alpha, 1), Version::parse("1.0.0a1", ParseMode::MO2));
  ASSERT_EQ(Version(1, 0, 0, Alpha, 1), Version::parse("1.0.0alpha1", ParseMode::MO2));
  ASSERT_EQ(Version(1, 0, 0, Beta, 2), Version::parse("1.0.0beta2", ParseMode::MO2));
  ASSERT_EQ(Version(1, 0, 0, Beta, 2), Version::parse("1.0.0beta2", ParseMode::MO2));
  ASSERT_EQ(Version(2, 4, 1, 0, {ReleaseCandidate, 1, 1}),
            Version::parse("2.4.1rc1.1", ParseMode::MO2));
  ASSERT_EQ(Version(2, 2, 2, 1, Beta, 2),
            Version::parse("2.2.2.1beta2", ParseMode::MO2));
  ASSERT_EQ(Version(2, 5, 2, ReleaseCandidate, 1),
            Version::parse("v2.5.2rc1", ParseMode::MO2));
  ASSERT_EQ(Version(2, 5, 2, ReleaseCandidate, 2),
            Version::parse("2.5.2rc2", ParseMode::MO2));
}

TEST(VersioningTest, VersionString)
{
  ASSERT_EQ("1.0.0", Version(1, 0, 0).string());
  ASSERT_EQ("1.0.0-dev.1", Version(1, 0, 0, Development, 1).string());
  ASSERT_EQ("1.0.0-dev.2", Version(1, 0, 0, Development, 2).string());
  ASSERT_EQ("1.0.0-alpha", Version(1, 0, 0, Alpha).string());
  ASSERT_EQ("1.0.0-alpha.1.beta", Version(1, 0, 0, 0, {Alpha, 1, Beta}).string());
  ASSERT_EQ("1.0.0-beta.2", Version(1, 0, 0, Beta, 2).string());
  ASSERT_EQ("2.5.2-rc.1", Version(2, 5, 2, ReleaseCandidate, 1).string());
  ASSERT_EQ("2.5.2rc1",
            Version(2, 5, 2, ReleaseCandidate, 1).string(Version::FormatCondensed));
}

TEST(VersioningTest, VersionCompare)
{
  // shortcut
  using v = Version;

  // test from https://semver.org/
  ASSERT_TRUE(v(1, 0, 0) < v(2, 0, 0));
  ASSERT_TRUE(v(2, 0, 0) < v(2, 1, 0));
  ASSERT_TRUE(v(2, 1, 0) < v(2, 1, 1));

  ASSERT_TRUE(v(1, 0, 0, Alpha) < v(1, 0, 0, Alpha, 1));
  ASSERT_TRUE(v(1, 0, 0, Alpha, 1) < v(1, 0, 0, 0, {Alpha, Beta}));
  ASSERT_TRUE(v(1, 0, 0, 0, {Alpha, Beta}) < v(1, 0, 0, 1));
  ASSERT_TRUE(v(1, 0, 0, Beta) < v(1, 0, 0, Beta, 2));
  ASSERT_TRUE(v(1, 0, 0, Beta, 2) < v(1, 0, 0, Beta, 11));
  ASSERT_TRUE(v(1, 0, 0, Beta, 11) < v(1, 0, 0, ReleaseCandidate, 1));
  ASSERT_TRUE(v(1, 0, 0, ReleaseCandidate, 0) < v(1, 0, 0));

  ASSERT_TRUE(v(2, 4, 1, 0, {ReleaseCandidate, 1, 0}) ==
              v(2, 4, 1, ReleaseCandidate, 1));
  ASSERT_TRUE(v(2, 4, 1, 0, {ReleaseCandidate, 1, 0}) <
              v(2, 4, 1, 0, {ReleaseCandidate, 1, 1}));
  ASSERT_TRUE(v(2, 4, 1, ReleaseCandidate, 1) <
              v(2, 4, 1, 0, {ReleaseCandidate, 1, 1}));
  ASSERT_TRUE(v(1, 0, 0) < v(2, 0, 0, Alpha));
}

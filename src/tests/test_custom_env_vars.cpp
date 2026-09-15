#include <gtest/gtest.h>

#include <QMap>
#include <QString>

#include "envvars.h"

using CustomEnvVars::parseCustomEnvVars;

TEST(CustomEnvVars, EmptyInputSucceeds)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_TRUE(parseCustomEnvVars(QString(), out, error));
  EXPECT_TRUE(out.isEmpty());
  EXPECT_TRUE(error.isEmpty());
  EXPECT_TRUE(parseCustomEnvVars(QStringLiteral("   "), out, error));
  EXPECT_TRUE(out.isEmpty());
}

TEST(CustomEnvVars, ValidPair)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_TRUE(parseCustomEnvVars(QStringLiteral("FOO=bar"), out, error));
  EXPECT_EQ(1, out.size());
  EXPECT_EQ(QStringLiteral("bar"), out.value(QStringLiteral("FOO")));
}

TEST(CustomEnvVars, MultiplePairs)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_TRUE(parseCustomEnvVars(QStringLiteral("FOO=bar BAZ=qux"), out, error));
  EXPECT_EQ(2, out.size());
  EXPECT_EQ(QStringLiteral("bar"), out.value(QStringLiteral("FOO")));
  EXPECT_EQ(QStringLiteral("qux"), out.value(QStringLiteral("BAZ")));
}

TEST(CustomEnvVars, QuotedSpaces)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_TRUE(
      parseCustomEnvVars(QStringLiteral("BAZ='lorem ipsum'"), out, error));
  EXPECT_EQ(QStringLiteral("lorem ipsum"), out.value(QStringLiteral("BAZ")));

  EXPECT_TRUE(parseCustomEnvVars(QStringLiteral("FOO=\"a b\""), out, error));
  EXPECT_EQ(QStringLiteral("a b"), out.value(QStringLiteral("FOO")));
}

TEST(CustomEnvVars, EmptyValueSetsEmptyString)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_TRUE(parseCustomEnvVars(QStringLiteral("FOO="), out, error));
  ASSERT_TRUE(out.contains(QStringLiteral("FOO")));
  EXPECT_EQ(QStringLiteral(""), out.value(QStringLiteral("FOO")));

  EXPECT_TRUE(parseCustomEnvVars(QStringLiteral("FOO=''"), out, error));
  ASSERT_TRUE(out.contains(QStringLiteral("FOO")));
  EXPECT_EQ(QStringLiteral(""), out.value(QStringLiteral("FOO")));
}

TEST(CustomEnvVars, DuplicateOverrideWins)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_TRUE(
      parseCustomEnvVars(QStringLiteral("FOO=one FOO=two"), out, error));
  EXPECT_EQ(1, out.size());
  EXPECT_EQ(QStringLiteral("two"), out.value(QStringLiteral("FOO")));
}

TEST(CustomEnvVars, MissingEqualsFails)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_FALSE(parseCustomEnvVars(QStringLiteral("NOEQUALS"), out, error));
  EXPECT_EQ(QStringLiteral("NOEQUALS"), error);
}

TEST(CustomEnvVars, InvalidKeyFails)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_FALSE(parseCustomEnvVars(QStringLiteral("1BAD=x"), out, error));
  EXPECT_EQ(QStringLiteral("1BAD=x"), error);
}

TEST(CustomEnvVars, FirstInvalidTokenReported)
{
  QMap<QString, QString> out;
  QString error;
  EXPECT_FALSE(
      parseCustomEnvVars(QStringLiteral("FOO=bar NOEQUALS 1BAD=x"), out, error));
  EXPECT_EQ(QStringLiteral("NOEQUALS"), error);
}

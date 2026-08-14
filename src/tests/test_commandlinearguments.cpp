#include "commandlinearguments.h"

#include <QByteArray>
#include <gtest/gtest.h>

#include <vector>

namespace
{

std::optional<QStringList>
decode(const std::vector<QByteArray>& encoded, QString* error = nullptr)
{
  std::vector<QByteArray> storage = encoded;
  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (QByteArray& argument : storage) {
    argv.push_back(argument.data());
  }
  return cl::decodeUnixArguments(static_cast<int>(argv.size()), argv.data(),
                                 error);
}

}  // namespace

TEST(CommandLineArguments, DecodesUtf8ArgvWithoutLosingBoundaries)
{
  const std::vector<QByteArray> encoded = {
      QString::fromUtf8("/tmp/Fluorine '\"\\ 🧪").toUtf8(),
      QByteArrayLiteral("-i"),
      QByteArray(), QByteArrayLiteral("run"),
      QString::fromUtf8("/tmp/Nérévar Moon/🧪").toUtf8(),
      QByteArrayLiteral("$;&|<>*?[]()'\"\\"), QByteArrayLiteral("a\tb"),
  };

  const auto decoded = decode(encoded);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded,
            QStringList({QStringLiteral("-i"), QString{}, QStringLiteral("run"),
                         QString::fromUtf8("/tmp/Nérévar Moon/🧪"),
                         QStringLiteral("$;&|<>*?[]()'\"\\"),
                         QStringLiteral("a\tb")}));
}

TEST(CommandLineArguments, RejectsInvalidSystemEncoding)
{
  QString error;
  const auto decoded = decode(
      {QByteArrayLiteral("fluorine-manager"), QByteArray("\xc3\x28", 2)},
      &error);
  EXPECT_FALSE(decoded);
  EXPECT_FALSE(error.isEmpty());
}

TEST(CommandLineArguments, StopsManagerParsingAtBareExecutable)
{
  const QStringList hostileChildArguments = {
      QStringLiteral("--profile"), QStringLiteral("child"),
      QStringLiteral("--help"), QStringLiteral("--multiple"), QString{},
      QString::fromUtf8("Nérévar 🧪"), QStringLiteral("--profile"),
  };
  QStringList arguments = {
      QStringLiteral("--profile"), QStringLiteral("manager"),
      QStringLiteral("--logs"), QStringLiteral("/bin/echo"),
  };
  arguments.append(hostileChildArguments);

  const auto partition = cl::partitionProcessArguments(arguments);
  EXPECT_EQ(partition.managerArguments,
            QStringList({QStringLiteral("--profile"),
                         QStringLiteral("manager"), QStringLiteral("--logs")}));
  QStringList expectedInvocation = {QStringLiteral("/bin/echo")};
  expectedInvocation.append(hostileChildArguments);
  EXPECT_EQ(partition.invocation, expectedInvocation);
}

TEST(CommandLineArguments, PreservesGlobalOptionValueAndSeparatorSemantics)
{
  EXPECT_EQ(cl::partitionProcessArguments(
                {QStringLiteral("--instance"), QString{},
                 QStringLiteral("/bin/echo"), QStringLiteral("--profile")})
                .invocation,
            QStringList({QStringLiteral("/bin/echo"),
                         QStringLiteral("--profile")}));
  EXPECT_EQ(cl::partitionProcessArguments(
                {QStringLiteral("--instance"), QStringLiteral("--multiple"),
                 QStringLiteral("--"), QStringLiteral("/bin/echo"),
                 QStringLiteral("--help")})
                .managerArguments,
            QStringList({QStringLiteral("--instance"),
                         QStringLiteral("--multiple")}));
  EXPECT_EQ(cl::partitionProcessArguments(
                {QStringLiteral("--instance"), QStringLiteral("--multiple"),
                 QStringLiteral("--"), QStringLiteral("/bin/echo"),
                 QStringLiteral("--help")})
                .invocation,
            QStringList({QStringLiteral("/bin/echo"),
                         QStringLiteral("--help")}));
}

TEST(CommandLineArguments, ForwardedEnvelopeRoundTripsExactArguments)
{
  const QStringList arguments = {
      QStringLiteral("run"), QStringLiteral("--arguments"), QString{},
      QString::fromUtf8("/tmp/Grüße 🧪"),
      QStringLiteral("$;&|<>*?[]()'\"\\"), QStringLiteral("a\tb"),
      QStringLiteral("--option-looking"), QStringLiteral("trailing\\"),
  };

  const auto message = cl::encodeForwardedArguments(arguments);
  ASSERT_TRUE(message);
  EXPECT_TRUE(cl::isForwardedArgumentsMessage(*message));
  EXPECT_EQ(cl::decodeForwardedArguments(*message), arguments);
}

TEST(CommandLineArguments, RejectsMalformedOrUnsafeEnvelopes)
{
  EXPECT_FALSE(cl::isForwardedArgumentsMessage(QStringLiteral("run tool")));
  EXPECT_FALSE(cl::decodeForwardedArguments(QStringLiteral("run tool")));
  EXPECT_FALSE(cl::decodeForwardedArguments(
      QStringLiteral("@fluorine-command-argv-v1@not-json")));
  EXPECT_FALSE(cl::decodeForwardedArguments(
      QStringLiteral("@fluorine-command-argv-v1@[]")));
  EXPECT_FALSE(cl::decodeForwardedArguments(
      QStringLiteral("@fluorine-command-argv-v1@[\"run\",1]")));
  EXPECT_FALSE(cl::decodeForwardedArguments(
      QStringLiteral("@fluorine-command-argv-v1@[\"run\",\"\\u0000\"]")));
  EXPECT_FALSE(cl::encodeForwardedArguments({}));
  EXPECT_FALSE(cl::encodeForwardedArguments(
      {QStringLiteral("run"), QString(QChar::Null)}));

  QStringList tooMany(cl::MaximumForwardedArguments + 1,
                      QStringLiteral("x"));
  EXPECT_FALSE(cl::encodeForwardedArguments(tooMany));
}

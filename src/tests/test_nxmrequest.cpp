#include "nxmrequest.h"

#include <gtest/gtest.h>

TEST(NxmRequest, PreservesRepositoryLinks)
{
  const QString nxm = QStringLiteral(
      "nxm://game/mods/12/files/34?key=a%2Bb&expires=56&user_id=78");
  const auto nxmRequest = NxmRequest::parse(nxm);
  ASSERT_TRUE(nxmRequest);
  EXPECT_EQ(nxmRequest->kind, NxmRequest::Kind::RepositoryFile);
  EXPECT_EQ(nxmRequest->target, nxm);

  const QString modl =
      QStringLiteral("modl://game/mods/12/files/34?token=value");
  const auto modlRequest = NxmRequest::parse(modl);
  ASSERT_TRUE(modlRequest);
  EXPECT_EQ(modlRequest->kind, NxmRequest::Kind::RepositoryFile);
  EXPECT_EQ(modlRequest->target, modl);

  const QString modlWithDirectQuery = QStringLiteral(
      "modl://game/mods/12/files/34?url=https%3A%2F%2Fexample.invalid%2Fother");
  const auto precedence = NxmRequest::parse(modlWithDirectQuery);
  ASSERT_TRUE(precedence);
  EXPECT_EQ(precedence->kind, NxmRequest::Kind::RepositoryFile);
  EXPECT_EQ(precedence->target, modlWithDirectQuery);
}

TEST(NxmRequest, DecodesDirectModlDownload)
{
  const QString message = QStringLiteral(
      "modl://openmw/?url=https%3A%2F%2Fexample.invalid%2FGr%C3%BC%C3%9Fe%2520"
      "archive.7z%3Fx%3D1%25262");
  const auto request = NxmRequest::parse(message);
  ASSERT_TRUE(request);
  EXPECT_EQ(request->kind, NxmRequest::Kind::DirectDownload);
  EXPECT_EQ(request->target,
            QString::fromUtf8(
                "https://example.invalid/Grüße%20archive.7z?x=1%262"));
}

TEST(NxmRequest, RejectsNonNxmInput)
{
  EXPECT_FALSE(NxmRequest::parse(QStringLiteral("https://example.invalid/file")));
  EXPECT_FALSE(NxmRequest::parse(QStringLiteral("not a URL")));
  EXPECT_FALSE(NxmRequest::parse(QStringLiteral("nxm:///mods/1/files/2")));
  EXPECT_FALSE(NxmRequest::parse(QStringLiteral("nxm://game/mods/x/files/2")));
  EXPECT_FALSE(NxmRequest::parse(QStringLiteral("nxm://game/mods/0/files/2")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("nxm://game/mods/2147483648/files/2")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("nxm://game/mods//1/files/2")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("nxm://game-name/mods/1/files/2")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("nxm://user@game/mods/1/files/2")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("nxm://game:123/mods/1/files/2")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("modl://game/path?url=https%3A%2F%2Fexample.invalid")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("modl://game/?url=file%3A%2F%2F%2Ftmp%2Fpayload")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("modl://game/?url=javascript%3Aalert%281%29")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("modl://game/?url=%2Frelative%2Fpayload")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("modl://game/?url=https%3A%2F%2Fuser%3Apass%40example.invalid")));
  EXPECT_FALSE(NxmRequest::parse(
      QStringLiteral("modl://game/?url=https%3A%2F%2Fexample.invalid%2Fa%0Ab")));
}

TEST(NxmRequest, RejectsUnsupportedCollectionLinks)
{
  for (const QString& link : {
           QStringLiteral("nxm://game/collections/abc/revisions/1"),
           QStringLiteral("nxm://game/collections/abc/revisions/1?key=secret"),
           QStringLiteral("NXM://GAME/COLLECTIONS/ABC/REVISIONS/1"),
           QStringLiteral("modl://game/collections/abc/revisions/1"),
           QStringLiteral("modl://game/collections/abc/revisions/1?url="
                          "https%3A%2F%2Fexample.invalid%2Farchive"),
       }) {
    EXPECT_FALSE(NxmRequest::parse(link)) << link.toStdString();
  }
}

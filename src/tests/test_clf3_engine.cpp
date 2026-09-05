#include "../src/clf3enginemanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <cstring>
#include <gtest/gtest.h>

namespace {
struct Response {
  QByteArray body;
  QNetworkReply::NetworkError error{QNetworkReply::NoError};
  bool stall{false};
};

class Reply : public QNetworkReply
{
public:
  Reply(const QNetworkRequest& request, const Response& response, QObject* parent)
      : QNetworkReply(parent), m_body(response.body)
  {
    setRequest(request);
    setUrl(request.url());
    open(QIODevice::ReadOnly);
    if (!response.stall) QTimer::singleShot(0, this, [this, response] {
      if (isFinished()) return;
      if (response.error != NoError) setError(response.error, "Simulated network failure");
      setFinished(true);
      emit readyRead();
      emit finished();
    });
  }
  void abort() override
  {
    if (isFinished()) return;
    setError(OperationCanceledError, "Cancelled");
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override { return m_body.size() - m_position + QNetworkReply::bytesAvailable(); }
protected:
  qint64 readData(char* data, qint64 size) override
  {
    const auto length = qMin(size, qint64(m_body.size() - m_position));
    if (!length) return -1;
    std::memcpy(data, m_body.constData() + m_position, length);
    m_position += length;
    return length;
  }
private:
  QByteArray m_body;
  qint64 m_position{};
};

class Network : public QNetworkAccessManager
{
public:
  QList<Response> responses;
  QList<QUrl> requests;
protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
  {
    requests.append(request.url());
    if (responses.isEmpty()) {
      ADD_FAILURE() << "Unexpected request: " << request.url().toString().toStdString();
      return new Reply(request, {{}, QNetworkReply::ContentNotFoundError}, this);
    }
    return new Reply(request, responses.takeFirst(), this);
  }
};

QByteArray release(const QByteArray& archive, const QString& tag = "0.2.5", int id = 1)
{
  return QJsonDocument(QJsonObject{
      {"tag_name", tag}, {"draft", false}, {"prerelease", false},
      {"assets", QJsonArray{QJsonObject{
          {"name", "clf3-linux-x64.zip"}, {"id", id},
          {"browser_download_url", "https://github.com/SulfurNitride/CLF3/releases/download/" + tag + "/clf3-linux-x64.zip"},
          {"digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex())}}}}
  }).toJson();
}

class Clf3Engine : public ::testing::Test
{
protected:
  QTemporaryDir directory;
  Network network;
  Clf3EngineManager manager{nullptr, &network, directory.filePath("cache")};
  QSignalSpy ready{&manager, &Clf3EngineManager::ready};
  QSignalSpy failed{&manager, &Clf3EngineManager::failed};

  QByteArray archive(const QString& tag = "0.2.5")
  {
    const QString output = directory.filePath("fixture.zip");
    QProcess process;
    process.start("python3", {"-c",
        "import sys,zipfile\nwith zipfile.ZipFile(sys.argv[1],'w') as z:\n"
        " z.writestr('clf3', '#!/bin/sh\\necho clf3 '+sys.argv[2]+'\\n')\n"
        " z.writestr('7zz', '#!/bin/sh\\nexit 0\\n')\n", output, tag});
    EXPECT_TRUE(process.waitForFinished(5000));
    EXPECT_EQ(process.exitCode(), 0);
    QFile file(output);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
  }
  void queue(const QByteArray& bytes, const QString& tag = "0.2.5", int id = 1)
  {
    network.responses.append({release(bytes, tag, id)});
    network.responses.append({bytes});
  }
  QString install()
  {
    queue(archive());
    manager.prepare();
    EXPECT_TRUE(ready.wait(10000));
    EXPECT_TRUE(failed.isEmpty());
    return ready.isEmpty() ? QString{} : ready.last().first().toString();
  }
};

TEST_F(Clf3Engine, DownloadsVerifiesAndCachesLatestRelease)
{
  const auto bytes = archive();
  queue(bytes);
  manager.prepare();
  ASSERT_TRUE(ready.wait(10000));
  ASSERT_TRUE(failed.isEmpty());
  const QString path = ready.first().first().toString();
  EXPECT_TRUE(QFileInfo(path).isExecutable());
  EXPECT_EQ(manager.cachedEnginePath(), path);
  ASSERT_EQ(network.requests.size(), 2);
  EXPECT_TRUE(network.requests.first().path().endsWith("/releases/latest"));
  network.responses.append({release(bytes)});
  manager.prepare();
  ASSERT_TRUE(ready.wait(5000));
  EXPECT_EQ(network.requests.size(), 3); // metadata checked again; no second asset download
  EXPECT_EQ(ready.last().first().toString(), path);
  Clf3EngineManager restarted(nullptr, &network, directory.filePath("cache"));
  EXPECT_EQ(restarted.cachedEnginePath(), path);
}

TEST_F(Clf3Engine, NewReleaseReplacesManifestAndKeepsExistingExecutable)
{
  const QString previous = install();
  queue(archive("0.2.6"), "0.2.6", 2);
  manager.prepare();
  ASSERT_TRUE(ready.wait(10000));
  EXPECT_TRUE(failed.isEmpty());
  EXPECT_NE(manager.cachedEnginePath(), previous);
  EXPECT_TRUE(QFileInfo(previous).isExecutable()); // another install may still be using it
}

TEST_F(Clf3Engine, BadChecksumPreservesWorkingEngine)
{
  const QString previous = install();
  const auto bytes = archive("0.2.6");
  network.responses.append({release(bytes, "0.2.6", 2)});
  network.responses.append({"corrupt download"});
  manager.prepare();
  ASSERT_TRUE(failed.wait(5000));
  EXPECT_TRUE(failed.first().first().toString().contains("SHA-256"));
  EXPECT_EQ(manager.cachedEnginePath(), previous);
  EXPECT_EQ(ready.count(), 1);
}

TEST_F(Clf3Engine, RejectsOldReleaseAndExecutableWithWrongVersion)
{
  network.responses.append({release({}, "0.2.4")});
  manager.prepare();
  ASSERT_TRUE(failed.wait(5000));
  EXPECT_EQ(network.requests.size(), 1);
  queue(archive("0.2.3"));
  manager.prepare();
  ASSERT_TRUE(failed.wait(10000));
  EXPECT_EQ(failed.count(), 2);
  EXPECT_TRUE(ready.isEmpty());
  EXPECT_TRUE(manager.cachedEnginePath().isEmpty());
}

TEST_F(Clf3Engine, NetworkFailureUsesOnlyPreviouslyVerifiedCache)
{
  network.responses.append({{}, QNetworkReply::HostNotFoundError});
  manager.prepare();
  ASSERT_TRUE(failed.wait(5000));
  failed.clear();
  const QString previous = install();
  network.responses.append({{}, QNetworkReply::HostNotFoundError});
  manager.prepare();
  ASSERT_TRUE(ready.wait(5000));
  EXPECT_TRUE(failed.isEmpty());
  EXPECT_EQ(ready.last().first().toString(), previous);
}

TEST_F(Clf3Engine, CancellationDuringDownloadPreservesCacheAndCanRetry)
{
  const QString previous = install();
  network.responses.append({release(archive("0.2.6"), "0.2.6", 2)});
  network.responses.append({{}, QNetworkReply::NoError, true});
  manager.prepare();
  QTest::qWait(50);
  ASSERT_EQ(network.requests.size(), 4);
  QSignalSpy cancelled(&manager, &Clf3EngineManager::cancelled);
  manager.cancel();
  manager.cancel();
  QTest::qWait(50);
  EXPECT_EQ(cancelled.count(), 1);
  EXPECT_TRUE(failed.isEmpty());
  EXPECT_EQ(ready.count(), 1);
  EXPECT_EQ(manager.cachedEnginePath(), previous);
  queue(archive("0.2.6"), "0.2.6", 2);
  manager.prepare();
  ASSERT_TRUE(ready.wait(10000));
  EXPECT_NE(manager.cachedEnginePath(), previous);
}

TEST_F(Clf3Engine, RejectsUntrustedAssetUrl)
{
  auto data = release(archive());
  data.replace("https://github.com/", "https://example.com/");
  network.responses.append({data});
  manager.prepare();
  ASSERT_TRUE(failed.wait(5000));
  EXPECT_EQ(network.requests.size(), 1);
  EXPECT_TRUE(ready.isEmpty());
}
}

// Opt-in integration check against the real published GitHub release. Uses a
// temporary cache and never changes the user's selected engine or installation.
TEST(Clf3EngineLive, DISABLED_DownloadsLatestPublishedRelease)
{
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  Clf3EngineManager manager(nullptr, nullptr, directory.path());
  QSignalSpy ready(&manager, &Clf3EngineManager::ready);
  QSignalSpy failed(&manager, &Clf3EngineManager::failed);
  QEventLoop loop;
  QObject::connect(&manager, &Clf3EngineManager::ready, &loop, &QEventLoop::quit);
  QObject::connect(&manager, &Clf3EngineManager::failed, &loop, &QEventLoop::quit);
  QTimer::singleShot(180000, &loop, &QEventLoop::quit);
  manager.prepare();
  loop.exec();
  ASSERT_TRUE(failed.isEmpty()) << failed.first().first().toString().toStdString();
  ASSERT_EQ(ready.count(), 1);
  EXPECT_TRUE(QFileInfo(ready.first().first().toString()).isExecutable());
  EXPECT_EQ(manager.cachedEnginePath(), ready.first().first().toString());
}

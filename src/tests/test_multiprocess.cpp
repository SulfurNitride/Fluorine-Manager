#include "multiprocess.h"
#include "multiprocessprotocol.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QLockFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <gtest/gtest.h>
#include <uibase/log.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace {

MOMultiProcess::Endpoint endpoint(const QTemporaryDir &temporary,
                                  const QString &key) {
  return {temporary.path(), key};
}

QString socketPath(const MOMultiProcess::Endpoint &endpoint) {
  return endpoint.directory + QStringLiteral("/") + endpoint.key +
         QStringLiteral(".sock");
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 3000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!predicate() && elapsed.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return predicate();
}

QByteArray frameWithPayload(const QByteArray &payload, quint8 version = 1,
                            quint8 type = 1) {
  QByteArray frame("FMIP", 4);
  frame.append(static_cast<char>(version));
  frame.append(static_cast<char>(type));
  const quint32 length = qToBigEndian(static_cast<quint32>(payload.size()));
  frame.append(reinterpret_cast<const char *>(&length), sizeof(length));
  frame.append(payload);
  return frame;
}

} // namespace

TEST(MultiProcessProtocol, RoundTripsUnicodeAndRejectsMalformedFrames) {
  const QString message = QString::fromUtf8("run /tmp/Grüße 'quoted' 🧪");
  const QByteArray frame = multiprocess_ipc::encodeMessage(message);
  ASSERT_FALSE(frame.isEmpty());

  for (qsizetype size = 0; size < frame.size(); ++size) {
    EXPECT_EQ(multiprocess_ipc::decodeMessage(frame.first(size)).status,
              multiprocess_ipc::DecodeStatus::Incomplete);
  }

  const auto decoded = multiprocess_ipc::decodeMessage(frame);
  ASSERT_EQ(decoded.status, multiprocess_ipc::DecodeStatus::Complete);
  EXPECT_EQ(decoded.message, message);

  EXPECT_EQ(multiprocess_ipc::decodeMessage(frame + 'x').status,
            multiprocess_ipc::DecodeStatus::Invalid);
  EXPECT_EQ(multiprocess_ipc::decodeMessage(frameWithPayload("x", 2)).status,
            multiprocess_ipc::DecodeStatus::Invalid);
  EXPECT_EQ(multiprocess_ipc::decodeMessage(frameWithPayload("x", 1, 2)).status,
            multiprocess_ipc::DecodeStatus::Invalid);
  EXPECT_EQ(
      multiprocess_ipc::decodeMessage(frameWithPayload(QByteArray("x\0y", 3)))
          .status,
      multiprocess_ipc::DecodeStatus::Invalid);
  EXPECT_EQ(multiprocess_ipc::decodeMessage(
                frameWithPayload(QByteArray::fromHex("c328")))
                .status,
            multiprocess_ipc::DecodeStatus::Invalid);

  const QString maximumMessage(multiprocess_ipc::MaximumPayload,
                               QLatin1Char('x'));
  const QByteArray maximumFrame =
      multiprocess_ipc::encodeMessage(maximumMessage);
  ASSERT_FALSE(maximumFrame.isEmpty());
  EXPECT_EQ(multiprocess_ipc::decodeMessage(maximumFrame).status,
            multiprocess_ipc::DecodeStatus::Complete);
  EXPECT_TRUE(multiprocess_ipc::encodeMessage(maximumMessage + QLatin1Char('x'))
                  .isEmpty());

  QByteArray oversized = frameWithPayload("x");
  const quint32 length =
      qToBigEndian(static_cast<quint32>(multiprocess_ipc::MaximumPayload + 1));
  std::memcpy(oversized.data() + 6, &length, sizeof(length));
  EXPECT_EQ(multiprocess_ipc::decodeMessage(oversized.first(10)).status,
            multiprocess_ipc::DecodeStatus::Invalid);
}

TEST(MultiProcess, CreatesOwnerOnlyEndpointAndPreservesClassification) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint =
      endpoint(temporary, QStringLiteral("classification"));

  MOMultiProcess primary(false, testEndpoint);
  EXPECT_FALSE(primary.ephemeral());
  EXPECT_FALSE(primary.secondary());

  struct stat state{};
  const QByteArray path = QFile::encodeName(socketPath(testEndpoint));
  ASSERT_EQ(::lstat(path.constData(), &state), 0);
  EXPECT_TRUE(S_ISSOCK(state.st_mode));
  EXPECT_EQ(state.st_uid, ::geteuid());
  EXPECT_EQ(state.st_mode & 0077, 0);

  MOMultiProcess ephemeral(false, testEndpoint);
  EXPECT_TRUE(ephemeral.ephemeral());
  EXPECT_FALSE(ephemeral.secondary());

  MOMultiProcess secondary(true, testEndpoint);
  EXPECT_FALSE(secondary.ephemeral());
  EXPECT_TRUE(secondary.secondary());
}

TEST(MultiProcess, MultipleInstanceDoesNotWaitForListenerReadiness) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint =
      endpoint(temporary, QStringLiteral("lock-without-listener"));
  QLockFile lock(testEndpoint.directory + QStringLiteral("/") +
                 testEndpoint.key + QStringLiteral(".lock"));
  lock.setStaleLockTime(0);
  ASSERT_TRUE(lock.tryLock(0));

  QElapsedTimer elapsed;
  elapsed.start();
  MOMultiProcess secondary(true, testEndpoint);
  EXPECT_TRUE(secondary.secondary());
  EXPECT_FALSE(secondary.ephemeral());
  EXPECT_LT(elapsed.elapsed(), 500);
}

TEST(MultiProcess, RefusesToReplaceALiveOwnedSocket) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint = endpoint(temporary, QStringLiteral("live-socket"));
  const QString path = socketPath(testEndpoint);

  QLocalServer incumbent;
  incumbent.setSocketOptions(QLocalServer::NoOptions);
  ASSERT_TRUE(incumbent.listen(path));

  struct stat before{};
  const QByteArray encoded = QFile::encodeName(path);
  ASSERT_EQ(::lstat(encoded.constData(), &before), 0);
  EXPECT_THROW(MOMultiProcess(false, testEndpoint), std::exception);

  struct stat after{};
  ASSERT_EQ(::lstat(encoded.constData(), &after), 0);
  EXPECT_EQ(before.st_dev, after.st_dev);
  EXPECT_EQ(before.st_ino, after.st_ino);

  QLocalSocket client;
  client.connectToServer(path);
  ASSERT_TRUE(client.waitForConnected(1000));
}

TEST(MultiProcess, ForwardsOneFramedMessageAndAcknowledgesIt) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint = endpoint(temporary, QStringLiteral("forward"));
  MOMultiProcess primary(false, testEndpoint);
  MOMultiProcess secondary(false, testEndpoint);

  const QString expected = QString::fromUtf8("run /tmp/Grüße '$HOME;*.esp'");
  QString received;
  QObject::connect(&primary, &MOMultiProcess::messageSent,
                   [&](const QString &message) { received = message; });

  auto result = std::async(std::launch::async,
                           [&] { return secondary.sendMessage(expected); });
  ASSERT_TRUE(waitUntil([&] {
    return !received.isEmpty() &&
           result.wait_for(std::chrono::milliseconds(0)) ==
               std::future_status::ready;
  }));
  EXPECT_TRUE(result.get());
  EXPECT_EQ(received, expected);
}

TEST(MultiProcess, FragmentedClientDoesNotBlockTheEventLoop) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint = endpoint(temporary, QStringLiteral("fragmented"));
  MOMultiProcess primary(false, testEndpoint);

  QString received;
  QObject::connect(&primary, &MOMultiProcess::messageSent,
                   [&](const QString &message) { received = message; });

  QLocalSocket client;
  client.connectToServer(socketPath(testEndpoint), QIODevice::ReadWrite);
  ASSERT_TRUE(client.waitForConnected(1000));

  bool heartbeat = false;
  QTimer::singleShot(20, [&] { heartbeat = true; });
  const QByteArray frame =
      multiprocess_ipc::encodeMessage(QStringLiteral("refresh"));
  ASSERT_EQ(client.write(frame.first(3)), 3);
  ASSERT_TRUE(client.waitForBytesWritten(1000));
  EXPECT_TRUE(waitUntil([&] { return heartbeat; }, 500));
  EXPECT_TRUE(received.isEmpty());

  ASSERT_EQ(client.write(frame.sliced(3)), frame.size() - 3);
  ASSERT_TRUE(client.waitForBytesWritten(1000));
  ASSERT_TRUE(waitUntil([&] { return !received.isEmpty(); }));
  EXPECT_EQ(received, QStringLiteral("refresh"));
}

TEST(MultiProcess, RecoversOnlyAStaleOwnedSocket) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint = endpoint(temporary, QStringLiteral("stale"));
  const QByteArray path = QFile::encodeName(socketPath(testEndpoint));

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    QLockFile lock(testEndpoint.directory + QStringLiteral("/") +
                   testEndpoint.key + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
      ::_exit(2);
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      ::_exit(3);
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path))) {
      ::_exit(4);
    }
    std::memcpy(address.sun_path, path.constData(), path.size() + 1);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) !=
        0) {
      ::_exit(5);
    }
    ::close(fd);
    ::_exit(0);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);

  MOMultiProcess recovered(false, testEndpoint);
  EXPECT_FALSE(recovered.ephemeral());
  EXPECT_FALSE(recovered.secondary());
}

TEST(MultiProcess, RejectsMalformedClientsAndAcceptsTheNextMessage) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint = endpoint(temporary, QStringLiteral("malformed"));
  MOMultiProcess primary(false, testEndpoint);

  QStringList received;
  QObject::connect(&primary, &MOMultiProcess::messageSent,
                   [&](const QString &message) { received.append(message); });

  QLocalSocket oversized;
  oversized.connectToServer(socketPath(testEndpoint), QIODevice::ReadWrite);
  ASSERT_TRUE(oversized.waitForConnected(1000));
  QByteArray header = frameWithPayload("x").first(10);
  const quint32 declared =
      qToBigEndian(static_cast<quint32>(multiprocess_ipc::MaximumPayload + 1));
  std::memcpy(header.data() + 6, &declared, sizeof(declared));
  ASSERT_EQ(oversized.write(header), header.size());
  ASSERT_TRUE(oversized.waitForBytesWritten(1000));
  ASSERT_TRUE(waitUntil(
      [&] { return oversized.state() == QLocalSocket::UnconnectedState; }));
  EXPECT_TRUE(received.isEmpty());

  QLocalSocket incomplete;
  incomplete.connectToServer(socketPath(testEndpoint), QIODevice::WriteOnly);
  ASSERT_TRUE(incomplete.waitForConnected(1000));
  ASSERT_EQ(incomplete.write("FMI", 3), 3);
  ASSERT_TRUE(incomplete.waitForBytesWritten(1000));
  incomplete.abort();
  QCoreApplication::processEvents();
  EXPECT_TRUE(received.isEmpty());

  MOMultiProcess secondary(false, testEndpoint);
  auto result = std::async(std::launch::async,
                           [&] { return secondary.sendMessage("refresh"); });
  ASSERT_TRUE(waitUntil([&] {
    return received.size() == 1 &&
           result.wait_for(std::chrono::milliseconds(0)) ==
               std::future_status::ready;
  }));
  EXPECT_TRUE(result.get());
  EXPECT_EQ(received, QStringList{QStringLiteral("refresh")});
}

TEST(MultiProcess, RefusesToReplaceARegularFile) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto testEndpoint = endpoint(temporary, QStringLiteral("occupied"));
  const QString path = socketPath(testEndpoint);

  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write("keep"), 4);
  file.close();

  EXPECT_THROW(
      { MOMultiProcess candidate(false, testEndpoint); }, std::exception);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), QByteArray("keep"));

  const auto symlinkEndpoint = endpoint(temporary, QStringLiteral("symlink"));
  const QByteArray linkPath = QFile::encodeName(socketPath(symlinkEndpoint));
  const QByteArray targetPath = QFile::encodeName(path);
  ASSERT_EQ(::symlink(targetPath.constData(), linkPath.constData()), 0);
  EXPECT_THROW(
      { MOMultiProcess candidate(false, symlinkEndpoint); }, std::exception);

  struct stat linkState{};
  ASSERT_EQ(::lstat(linkPath.constData(), &linkState), 0);
  EXPECT_TRUE(S_ISLNK(linkState.st_mode));
}

TEST(MultiProcess, RefusesAnInsecureRuntimeDirectory) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QByteArray path = QFile::encodeName(temporary.path());
  ASSERT_EQ(::chmod(path.constData(), 0755), 0);
  EXPECT_THROW(
      MOMultiProcess(false, endpoint(temporary, QStringLiteral("insecure"))),
      std::exception);
  ASSERT_EQ(::chmod(path.constData(), 0700), 0);
}

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  MOBase::log::LoggerConfiguration logging;
  logging.name = "test_multiprocess";
  logging.maxLevel = MOBase::log::Warning;
  MOBase::log::createDefault(std::move(logging));
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

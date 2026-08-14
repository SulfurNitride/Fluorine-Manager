#include "multiprocess.h"
#include "multiprocessprotocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <uibase/exceptions.h>
#include <uibase/log.h>
#include <uibase/report.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {

constexpr int ConnectionTimeoutMs = 5000;
constexpr int MaximumConnections = 32;

using MOBase::reportError;

QByteArray encodedPath(const QString &path) { return QFile::encodeName(path); }

bool ownedPrivateDirectory(const QString &path, QString *error) {
  struct stat state{};
  const QByteArray encoded = encodedPath(path);
  if (::lstat(encoded.constData(), &state) != 0) {
    *error = QStringLiteral("cannot inspect runtime directory '%1': %2")
                 .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  if (!S_ISDIR(state.st_mode) || S_ISLNK(state.st_mode)) {
    *error =
        QStringLiteral("runtime location '%1' is not a directory").arg(path);
    return false;
  }
  if (state.st_uid != ::geteuid() || (state.st_mode & 0077) != 0) {
    *error =
        QStringLiteral("runtime directory '%1' must be owned by uid %2 with no "
                       "group or other access")
            .arg(path)
            .arg(::geteuid());
    return false;
  }
  return true;
}

bool sameUserPeer(QLocalSocket &socket) {
  const qintptr descriptor = socket.socketDescriptor();
  if (descriptor < 0) {
    return false;
  }

  struct ucred credentials{};
  socklen_t size = sizeof(credentials);
  return ::getsockopt(static_cast<int>(descriptor), SOL_SOCKET, SO_PEERCRED,
                      &credentials, &size) == 0 &&
         size == sizeof(credentials) && credentials.uid == ::geteuid();
}

enum class EndpointState { Inactive, Live, Ambiguous };

EndpointState endpointState(const QString &serverPath) {
  QLocalSocket probe;
  probe.connectToServer(serverPath, QIODevice::ReadWrite);
  if (probe.waitForConnected(250)) {
    const bool authenticated = sameUserPeer(probe);
    probe.abort();
    return authenticated ? EndpointState::Live : EndpointState::Ambiguous;
  }

  const auto error = probe.error();
  probe.abort();
  if (error == QLocalSocket::ConnectionRefusedError ||
      error == QLocalSocket::ServerNotFoundError) {
    return EndpointState::Inactive;
  }
  return EndpointState::Ambiguous;
}

bool sameSocket(const struct stat &left, const struct stat &right) {
  return left.st_dev == right.st_dev && left.st_ino == right.st_ino &&
         left.st_uid == right.st_uid && S_ISSOCK(left.st_mode) &&
         S_ISSOCK(right.st_mode);
}

MOMultiProcess::DeliveryResult deliverMessage(const QString &serverPath,
                                              const QString &message,
                                              int timeoutMs, int attempts,
                                              QString *error) {
  if (error != nullptr) {
    error->clear();
  }
  const QByteArray frame = multiprocess_ipc::encodeMessage(message);
  if (frame.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("refusing to forward an empty or oversized command");
    }
    return MOMultiProcess::DeliveryResult::Rejected;
  }

  QLocalSocket socket;
  bool connected = false;
  for (int i = 0; i < attempts && !connected; ++i) {
    if (i > 0) {
      QThread::msleep(250);
    }
    socket.connectToServer(serverPath, QIODevice::ReadWrite);
    connected = socket.waitForConnected(timeoutMs);
  }

  if (!connected) {
    if (error != nullptr) {
      *error = QStringLiteral("failed to connect to running process: %1")
                   .arg(socket.errorString());
    }
    return MOMultiProcess::DeliveryResult::Unavailable;
  }
  if (!sameUserPeer(socket)) {
    if (error != nullptr) {
      *error = QStringLiteral("running process did not authenticate as this user");
    }
    socket.abort();
    return MOMultiProcess::DeliveryResult::Rejected;
  }

  const qint64 written = socket.write(frame);
  if (written != frame.size()) {
    if (error != nullptr) {
      *error = QStringLiteral("failed to communicate with running process: %1")
                   .arg(socket.errorString());
    }
    socket.abort();
    return MOMultiProcess::DeliveryResult::Unavailable;
  }
  while (socket.bytesToWrite() > 0) {
    if (!socket.waitForBytesWritten(timeoutMs)) {
      if (error != nullptr) {
        *error = QStringLiteral(
                     "running process did not receive the complete command in time: %1")
                     .arg(socket.errorString());
      }
      // Bytes still buffered in this process cannot complete the framed
      // message after abort, so retrying cannot duplicate an admitted frame.
      socket.abort();
      return MOMultiProcess::DeliveryResult::Unavailable;
    }
  }

  QByteArray reply;
  const qsizetype replySize = multiprocess_ipc::acceptedReply().size();
  while (reply.size() < replySize) {
    if (socket.bytesAvailable() == 0 &&
        !socket.waitForReadyRead(timeoutMs)) {
      if (error != nullptr) {
        *error =
            QStringLiteral("running process did not acknowledge the command: %1")
                .arg(socket.errorString());
      }
      socket.abort();
      return MOMultiProcess::DeliveryResult::Indeterminate;
    }
    reply.append(socket.readAll());
  }

  const bool accepted = multiprocess_ipc::isAcceptedReply(reply);
  const bool rejected = reply == multiprocess_ipc::rejectedReply();
  socket.abort();
  if (!accepted && error != nullptr) {
    *error = rejected
                 ? QStringLiteral("running process rejected the forwarded command")
                 : QStringLiteral("running process returned an invalid acknowledgement");
  }
  if (accepted) {
    return MOMultiProcess::DeliveryResult::Accepted;
  }
  return rejected ? MOMultiProcess::DeliveryResult::Rejected
                  : MOMultiProcess::DeliveryResult::Indeterminate;
}

} // namespace

MOMultiProcess::MOMultiProcess(bool allowMultiple, QObject *parent)
    : MOMultiProcess(allowMultiple, defaultEndpoint(), parent) {}

MOMultiProcess::MOMultiProcess(bool allowMultiple, Endpoint endpoint,
                               QObject *parent)
    : QObject(parent), m_Endpoint(std::move(endpoint)) {
  initialize(allowMultiple);
}

MOMultiProcess::~MOMultiProcess() = default;

void MOMultiProcess::setMessageHandler(
    std::function<bool(const QString &)> handler) {
  m_MessageHandler = std::move(handler);
}

MOMultiProcess::Endpoint MOMultiProcess::defaultEndpoint() {
  return {QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation),
          QStringLiteral("fluorine-manager-%1").arg(::geteuid())};
}

MOMultiProcess::DeliveryResult
MOMultiProcess::trySendToPrimary(const QString &message, QString *error) {
  return trySendToPrimary(message, defaultEndpoint(), error);
}

MOMultiProcess::DeliveryResult
MOMultiProcess::trySendToPrimary(const QString &message,
                                 const Endpoint &endpoint, QString *error) {
  if (error != nullptr) {
    error->clear();
  }
  QString endpointError;
  if (endpoint.directory.isEmpty() || endpoint.key.isEmpty() ||
      endpoint.key.contains('/') ||
      !ownedPrivateDirectory(endpoint.directory, &endpointError)) {
    if (endpointError.isEmpty()) {
      endpointError = QStringLiteral("invalid local IPC endpoint");
    }
    if (error != nullptr) {
      *error = endpointError;
    }
    return DeliveryResult::Unavailable;
  }

  const QString serverPath =
      QDir(endpoint.directory).filePath(endpoint.key + QStringLiteral(".sock"));
  struct stat state{};
  const QByteArray encoded = encodedPath(serverPath);
  if (::lstat(encoded.constData(), &state) != 0) {
    if (error != nullptr) {
      *error = errno == ENOENT
                   ? QStringLiteral("no running primary process is available")
                   : QStringLiteral("cannot inspect primary IPC endpoint: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
    }
    return DeliveryResult::Unavailable;
  }
  if (!S_ISSOCK(state.st_mode) || state.st_uid != ::geteuid() ||
      (state.st_mode & 0077) != 0) {
    if (error != nullptr) {
      *error = QStringLiteral("refusing an unsafe primary IPC endpoint");
    }
    return DeliveryResult::Rejected;
  }

  return deliverMessage(serverPath, message, 1500, 1, error);
}

bool MOMultiProcess::mayHaveDelivered(DeliveryResult result) noexcept {
  return result == DeliveryResult::Accepted ||
         result == DeliveryResult::Indeterminate;
}

void MOMultiProcess::initialize(bool allowMultiple) {
  QString error;
  if (m_Endpoint.directory.isEmpty() || m_Endpoint.key.isEmpty() ||
      m_Endpoint.key.contains('/') ||
      !ownedPrivateDirectory(m_Endpoint.directory, &error)) {
    if (error.isEmpty()) {
      error = QStringLiteral("invalid local IPC endpoint");
    }
    throw MOBase::MyException(tr("IPC error: %1").arg(error));
  }

  const QDir runtime(m_Endpoint.directory);
  m_ServerPath = runtime.filePath(m_Endpoint.key + QStringLiteral(".sock"));
  m_LockPath = runtime.filePath(m_Endpoint.key + QStringLiteral(".lock"));
  m_PrimaryLock = std::make_unique<QLockFile>(m_LockPath);
  m_PrimaryLock->setStaleLockTime(0);

  const auto classifyExistingPrimary = [&] {
    m_Primary = false;
    m_Ephemeral = !allowMultiple;
  };

  if (!m_PrimaryLock->tryLock(0)) {
    if (m_PrimaryLock->error() != QLockFile::LockFailedError) {
      throw MOBase::MyException(
          tr("failed to inspect the single-instance lock '%1'")
              .arg(m_LockPath));
    }

    // A multiple-instance process is deliberately disconnected from the
    // primary and does not depend on its listener reaching readiness.
    if (allowMultiple) {
      classifyExistingPrimary();
      return;
    }

    // The primary takes the lifetime lock immediately before it binds the
    // listener. Give a concurrently starting owner time to reach listen().
    for (int elapsed = 0; elapsed < ConnectionTimeoutMs; elapsed += 50) {
      if (endpointState(m_ServerPath) == EndpointState::Live) {
        classifyExistingPrimary();
        return;
      }
      if (m_PrimaryLock->tryLock(0)) {
        startPrimary();
        return;
      }
      QThread::msleep(50);
    }

    throw MOBase::MyException(
        tr("another Fluorine process owns the instance lock but its IPC "
           "listener did not become available"));
  }

  startPrimary();
}

void MOMultiProcess::startPrimary() {
  struct stat existing{};
  const QByteArray encoded = encodedPath(m_ServerPath);
  if (::lstat(encoded.constData(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode) || existing.st_uid != ::geteuid()) {
      throw MOBase::MyException(
          tr("refusing to replace non-socket or foreign IPC endpoint '%1'")
              .arg(m_ServerPath));
    }

    if (endpointState(m_ServerPath) != EndpointState::Inactive) {
      throw MOBase::MyException(
          tr("refusing to replace an active or ambiguous IPC endpoint '%1'")
              .arg(m_ServerPath));
    }

    struct stat current{};
    if (::lstat(encoded.constData(), &current) != 0 ||
        !sameSocket(existing, current) ||
        !QLocalServer::removeServer(m_ServerPath)) {
      throw MOBase::MyException(
          tr("failed to remove stale IPC endpoint '%1'").arg(m_ServerPath));
    }
  } else if (errno != ENOENT) {
    throw MOBase::MyException(
        tr("failed to inspect IPC endpoint '%1': %2")
            .arg(m_ServerPath, QString::fromLocal8Bit(std::strerror(errno))));
  }

  connect(&m_Server, &QLocalServer::newConnection, this,
          &MOMultiProcess::acceptConnections, Qt::QueuedConnection);
  m_Server.setMaxPendingConnections(MaximumConnections);
  // Bind the requested pathname directly. UserAccessOption uses a temporary
  // socket and rename on Unix, which can replace a raced-in live endpoint.
  // The validated 0700 runtime directory protects the bind; chmod below
  // tightens the socket itself before any event can be dispatched.
  m_Server.setSocketOptions(QLocalServer::NoOptions);

  if (!m_Server.listen(m_ServerPath)) {
    throw MOBase::MyException(
        tr("failed to create single-instance listener without replacing an "
           "existing endpoint: %1")
            .arg(m_Server.errorString()));
  }

  if (::chmod(encoded.constData(), 0600) != 0) {
    const QString error = QString::fromLocal8Bit(std::strerror(errno));
    m_Server.close();
    QLocalServer::removeServer(m_ServerPath);
    throw MOBase::MyException(
        tr("failed to restrict single-instance listener permissions: %1")
            .arg(error));
  }

  struct stat state{};
  if (::lstat(encoded.constData(), &state) != 0 || !S_ISSOCK(state.st_mode) ||
      state.st_uid != ::geteuid() || (state.st_mode & 0077) != 0) {
    m_Server.close();
    QLocalServer::removeServer(m_ServerPath);
    throw MOBase::MyException(
        tr("single-instance listener permissions are not owner-only"));
  }

  m_Primary = true;
  m_Ephemeral = false;
}

bool MOMultiProcess::sendMessage(const QString &message) {
  if (m_Primary) {
    return false;
  }

  QString error;
  const DeliveryResult result =
      deliverMessage(m_ServerPath, message, ConnectionTimeoutMs, 2, &error);
  if (result == DeliveryResult::Indeterminate) {
    MOBase::log::warn(
        "forwarded command was fully sent but not acknowledged; not retrying");
  } else if (result != DeliveryResult::Accepted) {
    reportError(tr("failed to forward command: %1").arg(error));
  }
  return mayHaveDelivered(result);
}

void MOMultiProcess::acceptConnections() {
  while (QLocalSocket *socket = m_Server.nextPendingConnection()) {
    if (m_ConnectionBuffers.size() >= MaximumConnections ||
        !sameUserPeer(*socket)) {
      MOBase::log::warn("rejecting unauthenticated or excess IPC connection");
      socket->abort();
      socket->deleteLater();
      continue;
    }

    socket->setReadBufferSize(multiprocess_ipc::HeaderSize +
                              multiprocess_ipc::MaximumPayload + 1);
    m_ConnectionBuffers.insert(socket, {});

    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->setInterval(ConnectionTimeoutMs);
    m_ConnectionTimers.insert(socket, timer);

    connect(timer, &QTimer::timeout, this,
            [this, socket] { rejectConnection(socket, "timed out"); });
    connect(socket, &QLocalSocket::readyRead, this,
            [this, socket] { handleReadyRead(socket); });
    connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
      if (m_ConnectionBuffers.contains(socket)) {
        handleReadyRead(socket);
      }
      if (m_ConnectionBuffers.contains(socket)) {
        forgetConnection(socket);
        socket->deleteLater();
      }
    });
    connect(socket, &QLocalSocket::errorOccurred, this,
            [this, socket](QLocalSocket::LocalSocketError) {
              if (m_ConnectionBuffers.contains(socket)) {
                rejectConnection(socket, "socket error");
              }
            });
    timer->start();
    if (socket->bytesAvailable() > 0) {
      handleReadyRead(socket);
    }
  }
}

void MOMultiProcess::handleReadyRead(QLocalSocket *socket) {
  auto it = m_ConnectionBuffers.find(socket);
  if (it == m_ConnectionBuffers.end()) {
    return;
  }

  it.value().append(socket->readAll());
  if (it.value().size() >
      multiprocess_ipc::HeaderSize + multiprocess_ipc::MaximumPayload) {
    rejectConnection(socket, "frame exceeds limit");
    return;
  }

  const auto decoded = multiprocess_ipc::decodeMessage(it.value());
  if (decoded.status == multiprocess_ipc::DecodeStatus::Invalid) {
    rejectConnection(socket, "invalid frame");
  } else if (decoded.status == multiprocess_ipc::DecodeStatus::Complete) {
    acceptMessage(socket, decoded.message);
  }
}

void MOMultiProcess::rejectConnection(QLocalSocket *socket,
                                      const char *reason) {
  MOBase::log::debug("rejecting IPC connection: {}", reason);
  forgetConnection(socket);
  socket->abort();
  socket->deleteLater();
}

void MOMultiProcess::acceptMessage(QLocalSocket *socket,
                                   const QString &message) {
  forgetConnection(socket);
  const bool accepted = m_MessageHandler && m_MessageHandler(message);
  if (accepted) {
    MOBase::log::debug("accepted external IPC message ({} UTF-8 bytes)",
                       message.toUtf8().size());
    emit messageSent(message);
  } else {
    MOBase::log::debug("application rejected external IPC message");
  }

  const QByteArray reply = accepted ? multiprocess_ipc::acceptedReply()
                                    : multiprocess_ipc::rejectedReply();
  if (socket->write(reply) != reply.size()) {
    socket->abort();
    socket->deleteLater();
    return;
  }

  socket->flush();
  socket->disconnectFromServer();
  connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
  QTimer::singleShot(ConnectionTimeoutMs, socket, [socket] {
    socket->abort();
    socket->deleteLater();
  });
}

void MOMultiProcess::forgetConnection(QLocalSocket *socket) {
  if (QTimer *timer = m_ConnectionTimers.take(socket)) {
    timer->stop();
  }
  m_ConnectionBuffers.remove(socket);
  socket->disconnect(this);
}

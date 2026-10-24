#include "multiprocess.h"
#include "utility.h"

#include <QLocalSocket>
#include <QThread>
#include <log.h>
#include <report.h>

static const char s_Key[]  = "mo-43d1a3ad-eeb0-4818-97c9-eda5216c29b5";
static const int s_Timeout = 5000;

using MOBase::reportError;

// Liveness probe: a real primary owns a QLocalServer listening on s_Key.
// If we can connect, a primary exists. If not, the shm/socket is stale
// (prior crash left SysV segment + unix socket file behind on Linux).
static bool primaryAlive(const QString& key)
{
  QLocalSocket probe;
  probe.connectToServer(key, QIODevice::WriteOnly);
  const bool alive = probe.waitForConnected(500);
  if (alive) {
    probe.disconnectFromServer();
  }
  return alive;
}

QString MOMultiProcess::defaultKey()
{
  return QString::fromLatin1(s_Key);
}

bool MOMultiProcess::createSegment()
{
  if (!m_SharedMem.create(1)) {
    return false;
  }
  if (m_SharedMem.lock()) {
    *static_cast<char*>(m_SharedMem.data()) = 0;
    m_SharedMem.unlock();
  }
  return true;
}

void MOMultiProcess::setHeadlessPrimary(bool headless)
{
  if (!m_SharedMem.isAttached()) {
    MOBase::log::warn("cannot record headless state: shared memory not attached");
    return;
  }
  if (!m_SharedMem.lock()) {
    MOBase::log::warn("cannot record headless state: {}", m_SharedMem.errorString());
    return;
  }
  *static_cast<char*>(m_SharedMem.data()) = headless ? 1 : 0;
  m_SharedMem.unlock();
}

bool MOMultiProcess::primaryIsHeadless() const
{
  if (!m_SharedMem.isAttached() || !m_SharedMem.lock()) {
    return false;
  }
  const bool headless = *static_cast<const char*>(m_SharedMem.constData()) != 0;
  m_SharedMem.unlock();
  return headless;
}

MOMultiProcess::MOMultiProcess(bool allowMultiple, QObject* parent, const QString& key)
    : QObject(parent), m_Key(key)
{
  m_SharedMem.setKey(m_Key);

  if (createSegment()) {
    m_OwnsSM = true;
  } else {
    auto error = m_SharedMem.error();

    if (error == QSharedMemory::AlreadyExists && !allowMultiple) {
      // Primary instance likely exists. Try to attach as ephemeral process.
      if (m_SharedMem.attach()) {
        // Verify a primary is actually listening. Stale SysV shm on Linux
        // survives crashes — attach succeeds against a dead segment.
        if (primaryAlive(m_Key)) {
          m_Ephemeral = true;
        } else {
          MOBase::log::debug(
              "stale shared memory detected (no primary listener), "
              "claiming ownership");
          m_SharedMem.detach();
          if (createSegment()) {
            m_OwnsSM = true;
            error    = QSharedMemory::NoError;
          } else {
            // SHM reclaim failed — the SysV segment is orphaned because the
            // previous primary died without IPC_RMID, and only the creator
            // can remove it. The unix socket is the real liveness signal
            // and we already proved no listener exists, so claim primary
            // anyway. The orphan segment is harmless once we own the socket.
            MOBase::log::warn(
                "stale shared memory could not be reclaimed ({}), "
                "claiming primary via socket listener",
                m_SharedMem.errorString());
            m_OwnsSM = true;
            error    = QSharedMemory::NoError;
          }
        }
      } else {
        // Handle races with stale shared memory state:
        // between create() and attach(), the owner may have disappeared.
        auto attachError = m_SharedMem.error();

        if (attachError == QSharedMemory::NotFound ||
            attachError == QSharedMemory::KeyError ||
            attachError == QSharedMemory::UnknownError) {
          MOBase::log::debug(
              "shared memory attach race: {} ({}), retrying as owner",
              m_SharedMem.errorString(), static_cast<int>(attachError));
          if (createSegment()) {
            m_OwnsSM = true;
            error    = QSharedMemory::NoError;
          } else {
            error = m_SharedMem.error();
          }
        } else {
          MOBase::log::debug("shared memory attach failed: {} ({})",
                             m_SharedMem.errorString(),
                             static_cast<int>(attachError));
          error = attachError;
        }
      }
    }

    if (!m_OwnsSM && !m_Ephemeral && error != QSharedMemory::NoError &&
        error != QSharedMemory::AlreadyExists) {
      throw MOBase::MyException(tr("SHM error: %1").arg(m_SharedMem.errorString()));
    }
  }

  if (m_OwnsSM) {
    connect(&m_Server, SIGNAL(newConnection()), this, SLOT(receiveMessage()),
            Qt::QueuedConnection);
    // has to be called before listen
    m_Server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_Server.listen(m_Key)) {
      // Never remove the endpoint until we have proved it is stale. An
      // unconditional remove can unlink a live listener created by another
      // process during concurrent startup.
      if (primaryAlive(m_Key)) {
        MOBase::log::info(
            "another primary started concurrently, running as ephemeral");
        m_OwnsSM    = false;
        m_Ephemeral = true;
      } else {
        MOBase::log::debug("removing stale local-server endpoint after listen "
                           "failed: {}",
                           m_Server.errorString().toStdString());
        QLocalServer::removeServer(m_Key);

        if (!m_Server.listen(m_Key)) {
          // A competitor may have claimed the name between removeServer() and
          // our retry. Demote if it is alive; otherwise fail explicitly rather
          // than running a primary that cannot receive forwarded links.
          if (primaryAlive(m_Key)) {
            MOBase::log::info(
                "another primary claimed the listener, running as ephemeral");
            m_OwnsSM    = false;
            m_Ephemeral = true;
          } else {
            throw MOBase::MyException(
                tr("failed to create single-instance listener: %1")
                    .arg(m_Server.errorString()));
          }
        }
      }
    }
  }
}

void MOMultiProcess::sendMessage(const QString& message)
{
  if (m_OwnsSM) {
    // nobody there to receive the message
    return;
  }
  QLocalSocket socket(this);

  bool connected = false;
  for (int i = 0; i < 2 && !connected; ++i) {
    if (i > 0) {
      QThread::msleep(250);
    }

    // other process may be just starting up
    socket.connectToServer(m_Key, QIODevice::WriteOnly);
    connected = socket.waitForConnected(s_Timeout);
  }

  if (!connected) {
    reportError(
        tr("failed to connect to running process: %1").arg(socket.errorString()));
    return;
  }

  socket.write(message.toUtf8());
  if (!socket.waitForBytesWritten(s_Timeout)) {
    if (socket.bytesToWrite()) {
      reportError(tr("failed to communicate with running process: %1")
                      .arg(socket.errorString()));
    }
  }

  socket.disconnectFromServer();
  socket.waitForDisconnected();
}

void MOMultiProcess::receiveMessage()
{
  QLocalSocket* socket = m_Server.nextPendingConnection();
  if (!socket) {
    return;
  }

  if (!socket->waitForReadyRead(s_Timeout)) {
    // check if there are bytes available; if so, it probably means the data was
    // already received by the time waitForReadyRead() was called and the
    // connection has been closed
    const auto av = socket->bytesAvailable();

    if (av <= 0) {
      // primaryAlive() liveness probes connect then disconnect without sending
      // any data — these are expected, not errors. A real secondary that
      // failed to deliver its payload will leave a non-PeerClosed error.
      const auto err = socket->error();
      const bool isProbe = err == QLocalSocket::PeerClosedError ||
                           err == QLocalSocket::UnknownSocketError;
      if (isProbe) {
        MOBase::log::debug(
            "secondary closed without sending data (liveness probe)");
      } else {
        MOBase::log::error("failed to receive data from secondary process: {}",
                           socket->errorString());

        reportError(tr("failed to receive data from secondary process: %1")
                        .arg(socket->errorString()));
      }
      return;
    }
  }

  QString const message = QString::fromUtf8(socket->readAll().constData());
  emit messageSent(message);
  socket->disconnectFromServer();
}

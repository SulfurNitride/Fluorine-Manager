#include "multiprocess.h"
#include "utility.h"
#include <QLocalSocket>
#ifndef _WIN32
#include <QThread>
#endif
#include <log.h>
#include <report.h>

static const char s_Key[]  = "mo-43d1a3ad-eeb0-4818-97c9-eda5216c29b5";
static const int s_Timeout = 5000;

using MOBase::reportError;

MOMultiProcess::MOMultiProcess(bool allowMultiple, QObject* parent)
    : QObject(parent), m_Ephemeral(false), m_OwnsSM(false)
{
  m_SharedMem.setKey(s_Key);

  if (m_SharedMem.create(1)) {
    m_OwnsSM = true;
  } else {
    auto error = m_SharedMem.error();

    if (error == QSharedMemory::AlreadyExists && !allowMultiple) {
      // Primary instance likely exists. Try to attach as ephemeral process.
      if (m_SharedMem.attach()) {
        m_Ephemeral = true;
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
          if (m_SharedMem.create(1)) {
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
    m_Server.listen(s_Key);
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
#ifdef _WIN32
      Sleep(250);
#else
      QThread::msleep(250);
#endif
    }

    // other process may be just starting up
    socket.connectToServer(s_Key, QIODevice::WriteOnly);
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
      MOBase::log::error("failed to receive data from secondary process: {}",
                         socket->errorString());

      reportError(tr("failed to receive data from secondary process: %1")
                      .arg(socket->errorString()));
      return;
    }
  }

  QString message = QString::fromUtf8(socket->readAll().constData());
  emit messageSent(message);
  socket->disconnectFromServer();
}

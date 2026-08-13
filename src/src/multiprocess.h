#ifndef MODORGANIZER_MOMULTIPROCESS_INCLUDED
#define MODORGANIZER_MOMULTIPROCESS_INCLUDED

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QLocalSocket;
class QLockFile;
class QTimer;

/**
 * used to ensure only a single process of Mod Organizer is started and to
 * allow ephemeral processes to send messages to the primary (visible) one.
 * This way, other processes can start a download in the primary one
 **/
class MOMultiProcess : public QObject
{
  Q_OBJECT

public:
  struct Endpoint
  {
    QString directory;
    QString key;
  };

  // `allowMultiple`: if another process is running, run this one
  // disconnected from the primary process
  explicit MOMultiProcess(bool allowMultiple, QObject* parent = nullptr);
  MOMultiProcess(bool allowMultiple, Endpoint endpoint,
                 QObject* parent = nullptr);
  ~MOMultiProcess() override;

  static Endpoint defaultEndpoint();

  /**
   * @return true if this process's job is to forward data to the primary
   *              process through authenticated local IPC
   **/
  bool ephemeral() const { return m_Ephemeral; }

  // returns true if this is not the primary process, but was allowed because
  // of the AllowMultiple flag
  //
  bool secondary() const { return !m_Ephemeral && !m_Primary; }

  /**
   * send a message to the primary process. This can be used to transmit download urls
   *
   * @param message message to send
   **/
  bool sendMessage(const QString& message);
  void setMessageHandler(std::function<bool(const QString&)> handler);

signals:

  /**
   * @brief emitted when an ephemeral process has sent a message (to us)
   *
   * @param message the message we received
   **/
  void messageSent(const QString& message);

public slots:

private slots:

  void acceptConnections();

private:
  void initialize(bool allowMultiple);
  void startPrimary();
  void handleReadyRead(QLocalSocket* socket);
  void rejectConnection(QLocalSocket* socket, const char* reason);
  void acceptMessage(QLocalSocket* socket, const QString& message);
  void forgetConnection(QLocalSocket* socket);

  bool m_Ephemeral{false};
  bool m_Primary{false};
  Endpoint m_Endpoint;
  QString m_ServerPath;
  QString m_LockPath;
  std::unique_ptr<QLockFile> m_PrimaryLock;
  QLocalServer m_Server;
  QHash<QLocalSocket*, QByteArray> m_ConnectionBuffers;
  QHash<QLocalSocket*, QTimer*> m_ConnectionTimers;
  std::function<bool(const QString&)> m_MessageHandler;
};

#endif  // MODORGANIZER_MOMULTIPROCESS_INCLUDED

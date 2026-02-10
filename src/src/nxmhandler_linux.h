#ifndef NXMHANDLER_LINUX_H
#define NXMHANDLER_LINUX_H

#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>

struct NxmLink
{
  QString game_domain;
  uint64_t mod_id = 0;
  uint64_t file_id = 0;
  QString key;
  uint64_t expires = 0;

  static std::optional<NxmLink> parse(const QString& url);
  QString lookupKey() const;
};

Q_DECLARE_METATYPE(NxmLink)

class QLocalServer;
class QLocalSocket;

class NxmHandlerLinux : public QObject
{
  Q_OBJECT

public:
  explicit NxmHandlerLinux(QObject* parent = nullptr);
  ~NxmHandlerLinux() override;

  void registerHandler() const;
  bool startListener();

  static bool sendToSocket(const QString& url);
  static QString socketPath();

signals:
  void nxmReceived(NxmLink link);

private:
  void onNewConnection();
  void processSocketData(QLocalSocket* socket);

private:
  QLocalServer* m_server = nullptr;
};

#endif  // NXMHANDLER_LINUX_H

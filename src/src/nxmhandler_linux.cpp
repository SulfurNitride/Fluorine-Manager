#include "nxmhandler_linux.h"

#include <log.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

using namespace MOBase;

namespace
{
QString ensureDir(const QString& path)
{
  QDir dir(path);
  if (!dir.exists() && !QDir().mkpath(path)) {
    return {};
  }
  return path;
}

bool writeTextFile(const QString& path, const QString& content)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  QTextStream stream(&file);
  stream << content;
  return stream.status() == QTextStream::Ok;
}

void updateMimeAppsList(const QString& path, const QString& mimeType,
                        const QString& desktopFile)
{
  const QString entry = mimeType + "=" + desktopFile;

  QStringList lines;
  {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      const auto content = QString::fromUtf8(file.readAll());
      lines              = content.split('\n');
      while (!lines.isEmpty() && lines.back().isEmpty()) {
        lines.removeLast();
      }
    }
  }

  int defaultHeader = -1;
  bool replaced     = false;

  for (int i = 0; i <= lines.size(); ++i) {
    const bool atEnd = i == lines.size();
    const QString trimmed =
        atEnd ? QString() : lines.at(i).trimmed();

    if (!atEnd && trimmed.startsWith('[')) {
      if (trimmed == "[Default Applications]") {
        defaultHeader = i;
      }
      continue;
    }

    if (defaultHeader >= 0 && (atEnd || trimmed.startsWith('['))) {
      const int sectionEnd = atEnd ? lines.size() : i;
      for (int j = defaultHeader + 1; j < sectionEnd; ++j) {
        const QString item = lines.at(j).trimmed();
        if (item.startsWith(mimeType + "=")) {
          lines[j] = entry;
          replaced = true;
          break;
        }
      }
      if (!replaced) {
        lines.insert(defaultHeader + 1, entry);
      }
      break;
    }
  }

  if (defaultHeader < 0) {
    if (!lines.isEmpty() && !lines.back().isEmpty()) {
      lines.append(QString());
    }
    lines.append("[Default Applications]");
    lines.append(entry);
  }

  writeTextFile(path, lines.join('\n') + "\n");
}
}  // namespace

std::optional<NxmLink> NxmLink::parse(const QString& url)
{
  const QUrl parsed(url);
  if (!parsed.isValid() || parsed.scheme().compare("nxm", Qt::CaseInsensitive) != 0) {
    return {};
  }

  const QString gameDomain = parsed.host().trimmed();
  if (gameDomain.isEmpty()) {
    return {};
  }

  const QStringList parts =
      parsed.path().split('/', Qt::SkipEmptyParts);

  if (parts.size() != 4 || parts[0] != "mods" || parts[2] != "files") {
    return {};
  }

  bool modOk     = false;
  bool fileOk    = false;
  const uint64_t modId  = parts[1].toULongLong(&modOk);
  const uint64_t fileId = parts[3].toULongLong(&fileOk);
  if (!modOk || !fileOk) {
    return {};
  }

  const QUrlQuery query(parsed);
  const QString key = query.queryItemValue("key");
  if (key.isEmpty()) {
    return {};
  }

  bool expiresOk       = false;
  const uint64_t expires = query.queryItemValue("expires").toULongLong(&expiresOk);
  if (!expiresOk) {
    return {};
  }

  return NxmLink{gameDomain, modId, fileId, key, expires};
}

QString NxmLink::lookupKey() const
{
  return QString("%1:%2:%3").arg(game_domain).arg(mod_id).arg(file_id);
}

NxmHandlerLinux::NxmHandlerLinux(QObject* parent) : QObject(parent)
{
  qRegisterMetaType<NxmLink>("NxmLink");
}

NxmHandlerLinux::~NxmHandlerLinux()
{
  if (m_server != nullptr) {
    m_server->close();
    delete m_server;
    m_server = nullptr;
  }
}

QString NxmHandlerLinux::socketPath()
{
  const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
  if (!runtimeDir.isEmpty()) {
    return QDir(runtimeDir).filePath("mo2-nxm.sock");
  }

  return "/tmp/mo2-nxm.sock";
}

void NxmHandlerLinux::registerHandler() const
{
  const QString home = QDir::homePath();
  if (home.isEmpty()) {
    log::error("cannot register nxm handler: home path is empty");
    return;
  }

  const QString localBin   = ensureDir(home + "/.local/bin");
  const QString appsDir    = ensureDir(home + "/.local/share/applications");
  const QString configDir  = ensureDir(home + "/.config");

  if (localBin.isEmpty() || appsDir.isEmpty() || configDir.isEmpty()) {
    log::error("cannot register nxm handler: failed to create required directories");
    return;
  }

  const QString wrapperPath = localBin + "/mo2-nxm-handler";
  const QString executable  = QCoreApplication::applicationFilePath();
  const QString wrapper =
      QString("#!/bin/sh\nexec \"%1\" nxm-handle \"$@\"\n").arg(executable);

  if (!writeTextFile(wrapperPath, wrapper)) {
    log::error("failed to write nxm wrapper script '{}'", wrapperPath);
    return;
  }

  QFile::setPermissions(wrapperPath,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup | QFileDevice::ReadOther |
                            QFileDevice::ExeOther);

  const QString desktopPath = appsDir + "/mo2-nxm-handler.desktop";
  const QString desktop = "[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=Mod Organizer 2 NXM Handler\n"
                          "Exec=mo2-nxm-handler nxm-handle %u\n"
                          "MimeType=x-scheme-handler/nxm;\n"
                          "NoDisplay=true\n";

  if (!writeTextFile(desktopPath, desktop)) {
    log::error("failed to write nxm desktop entry '{}'", desktopPath);
    return;
  }

  updateMimeAppsList(configDir + "/mimeapps.list", "x-scheme-handler/nxm",
                     "mo2-nxm-handler.desktop");
  updateMimeAppsList(appsDir + "/mimeapps.list", "x-scheme-handler/nxm",
                     "mo2-nxm-handler.desktop");

  const auto result =
      QProcess::execute("update-desktop-database", QStringList() << appsDir);
  if (result != 0) {
    log::warn("update-desktop-database exited with code {}", result);
  }
}

bool NxmHandlerLinux::startListener()
{
  if (m_server != nullptr && m_server->isListening()) {
    return true;
  }

  if (m_server == nullptr) {
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this,
            &NxmHandlerLinux::onNewConnection);
  } else {
    m_server->close();
  }

  const QString path = socketPath();

  QLocalServer::removeServer(path);
  if (QFileInfo::exists(path)) {
    QFile::remove(path);
  }

  if (!m_server->listen(path)) {
    log::error("failed to start nxm listener on '{}': {}", path,
               m_server->errorString());
    return false;
  }

  log::info("nxm listener started on '{}'", path);
  return true;
}

void NxmHandlerLinux::onNewConnection()
{
  if (m_server == nullptr) {
    return;
  }

  while (QLocalSocket* socket = m_server->nextPendingConnection()) {
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
      processSocketData(socket);
    });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
  }
}

void NxmHandlerLinux::processSocketData(QLocalSocket* socket)
{
  while (socket->canReadLine()) {
    const QString line = QString::fromUtf8(socket->readLine()).trimmed();
    if (line.isEmpty()) {
      continue;
    }

    const auto link = NxmLink::parse(line);
    if (!link) {
      log::warn("received invalid nxm url on socket: {}", line);
      continue;
    }

    emit nxmReceived(*link);
  }
}

bool NxmHandlerLinux::sendToSocket(const QString& url)
{
  QLocalSocket socket;
  socket.connectToServer(socketPath(), QIODevice::WriteOnly);
  if (!socket.waitForConnected(1500)) {
    return false;
  }

  QByteArray payload = url.toUtf8();
  payload.append('\n');

  if (socket.write(payload) != payload.size()) {
    socket.abort();
    return false;
  }

  if (!socket.waitForBytesWritten(1500)) {
    socket.abort();
    return false;
  }

  socket.disconnectFromServer();
  return true;
}

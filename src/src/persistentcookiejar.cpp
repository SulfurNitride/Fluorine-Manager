#include "persistentcookiejar.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QNetworkCookie>
#include <QSaveFile>
#include <QtEndian>
#include <uibase/log.h>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace MOBase;

namespace
{
constexpr qint64 MaxCookieFileBytes  = 8 * 1024 * 1024;
constexpr quint32 MaxCookieCount     = 4096;
constexpr quint32 MaxCookieRawBytes  = 64 * 1024;

bool readUInt32(const QByteArray& bytes, qsizetype& offset, quint32& value)
{
  if (offset < 0 || bytes.size() - offset < static_cast<qsizetype>(sizeof(value))) {
    return false;
  }

  value = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar*>(bytes.constData() + offset));
  offset += sizeof(value);
  return true;
}

bool shouldPersist(const QNetworkCookie& cookie, const QDateTime& now)
{
  return !cookie.isSessionCookie() && cookie.expirationDate().isValid() &&
         cookie.expirationDate() > now;
}

bool isSafeCookieFile(const QFileInfo& info)
{
  return !info.isSymLink() && (!info.exists() || info.isFile());
}
}

PersistentCookieJar::PersistentCookieJar(const QString& fileName, QObject* parent)
    : QNetworkCookieJar(parent), m_FileName(fileName)
{
  restore();
}

PersistentCookieJar::~PersistentCookieJar()
{
  log::debug("save {}", m_FileName);
  save();
}

void PersistentCookieJar::clear()
{
  setAllCookies({});
  save();
}

bool PersistentCookieJar::save()
{
  QList<QByteArray> serialized;
  qint64 serializedSize = sizeof(quint32);
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (const QNetworkCookie& cookie : allCookies()) {
    if (!shouldPersist(cookie, now)) {
      continue;
    }

    const QByteArray raw = cookie.toRawForm();
    if (raw.isEmpty() || raw.size() > MaxCookieRawBytes) {
      log::error("failed to save cookies: invalid persistent cookie size");
      return false;
    }
    serializedSize += sizeof(quint32) + raw.size();
    if (serialized.size() >= MaxCookieCount ||
        serializedSize > MaxCookieFileBytes) {
      log::error("failed to save cookies: persistent cookie jar is too large");
      return false;
    }
    serialized.append(raw);
  }

  if (!isSafeCookieFile(QFileInfo(m_FileName))) {
    log::error("failed to save cookies: unsafe cookie jar path {}", m_FileName);
    return false;
  }

  QSaveFile file(m_FileName);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    log::error("failed to save cookies: couldn't create {}", m_FileName);
    return false;
  }

  const auto ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
  if (!file.setPermissions(ownerOnly)) {
    log::error("failed to save cookies: couldn't secure {}", m_FileName);
    file.cancelWriting();
    return false;
  }

  QDataStream data(&file);
  data << static_cast<quint32>(serialized.size());
  for (const QByteArray& raw : serialized) {
    data << raw;
  }

  if (data.status() != QDataStream::Ok || !file.flush()) {
    log::error("failed to save cookies: failed to write {}", m_FileName);
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    log::error("failed to save cookies: failed to commit {}", m_FileName);
    return false;
  }
  return true;
}

void PersistentCookieJar::restore()
{
#ifdef Q_OS_UNIX
  const QFileInfo info(m_FileName);
  if (!isSafeCookieFile(info)) {
    log::warn("ignoring unsafe cookie jar path {}", m_FileName);
    return;
  }
  if (!info.exists()) {
    return;
  }

  const QByteArray encodedPath = QFile::encodeName(m_FileName);
  const int descriptor =
      ::open(encodedPath.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
  if (descriptor == -1) {
    if (errno != ENOENT) {
      log::warn("ignoring unreadable or unsafe cookie jar {}", m_FileName);
    }
    return;
  }

  struct stat status {};
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_size < static_cast<off_t>(sizeof(quint32)) ||
      status.st_size > MaxCookieFileBytes) {
    ::close(descriptor);
    log::warn("ignoring invalid or unsafe cookie jar {}", m_FileName);
    return;
  }

  QFile file;
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    log::warn("ignoring unreadable cookie jar {}", m_FileName);
    return;
  }
  const qint64 size = status.st_size;
#else
  const QFileInfo info(m_FileName);
  if (!isSafeCookieFile(info)) {
    log::warn("ignoring unsafe cookie jar path {}", m_FileName);
    return;
  }
  if (!info.exists()) {
    return;
  }

  QFile file(m_FileName);
  if (!file.open(QIODevice::ReadOnly)) {
    // not necessarily a problem, the file may just not exist (yet)
    return;
  }

  const qint64 size = file.size();
  if (size < static_cast<qint64>(sizeof(quint32)) || size > MaxCookieFileBytes) {
    log::warn("ignoring invalid cookie jar {}", m_FileName);
    return;
  }
#endif

  const QByteArray bytes = file.readAll();
  if (bytes.size() != size) {
    log::warn("ignoring unreadable cookie jar {}", m_FileName);
    return;
  }

  qsizetype offset = 0;
  quint32 count     = 0;
  if (!readUInt32(bytes, offset, count) || count > MaxCookieCount) {
    log::warn("ignoring invalid cookie jar {}", m_FileName);
    return;
  }

  QList<QNetworkCookie> cookies;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (quint32 i = 0; i < count; ++i) {
    quint32 rawSize = 0;
    if (!readUInt32(bytes, offset, rawSize) || rawSize == 0 ||
        rawSize > MaxCookieRawBytes ||
        bytes.size() - offset < static_cast<qsizetype>(rawSize)) {
      log::warn("ignoring invalid cookie jar {}", m_FileName);
      return;
    }

    const QByteArray raw(bytes.constData() + offset, rawSize);
    offset += rawSize;

    const QList<QNetworkCookie> parsed = QNetworkCookie::parseCookies(raw);
    if (parsed.size() != 1) {
      log::warn("ignoring invalid cookie jar {}", m_FileName);
      return;
    }
    if (shouldPersist(parsed.front(), now)) {
      cookies.append(parsed.front());
    }
  }

  if (offset != bytes.size()) {
    log::warn("ignoring invalid cookie jar {}", m_FileName);
    return;
  }

  setAllCookies(cookies);
}

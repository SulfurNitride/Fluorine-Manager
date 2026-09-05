#include "pefileutils.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
bool peFlagsOffset(const QByteArray& contents, qsizetype* flagsOffset)
{
  if (contents.size() < 64 || contents.left(2) != QByteArrayLiteral("MZ"))
    return false;
  const auto u8 = [&contents](qsizetype offset) {
    return static_cast<quint32>(
        static_cast<unsigned char>(contents.at(offset)));
  };
  const quint32 peOffset = u8(0x3c) | (u8(0x3d) << 8) | (u8(0x3e) << 16)
                           | (u8(0x3f) << 24);
  if (peOffset > static_cast<quint32>(contents.size() - 24)
      || contents.mid(static_cast<qsizetype>(peOffset), 4)
             != QByteArrayLiteral("PE\0\0"))
    return false;
  const qsizetype offset = static_cast<qsizetype>(peOffset) + 22;
  if (offset + 2 > contents.size()) return false;
  if (flagsOffset) *flagsOffset = offset;
  return true;
}

quint16 peFlags(const QByteArray& contents, qsizetype offset)
{
  return static_cast<quint16>(
             static_cast<unsigned char>(contents.at(offset)))
         | (static_cast<quint16>(
                static_cast<unsigned char>(contents.at(offset + 1)))
            << 8);
}
}

bool peLargeAddressAware(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QByteArray contents = file.readAll();
  qsizetype offset = 0;
  return peFlagsOffset(contents, &offset) && (peFlags(contents, offset) & 0x20u);
}

bool setPeLargeAddressAware(const QString& path, QString* error)
{
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    if (error) *error = QString("Cannot read %1").arg(path);
    return false;
  }
  QByteArray contents = input.readAll();
  const auto permissions = input.permissions();
  input.close();
  qsizetype offset = 0;
  if (!peFlagsOffset(contents, &offset)) {
    if (error) *error = QString("Invalid PE header in %1").arg(path);
    return false;
  }
  quint16 flags = peFlags(contents, offset);
  if (flags & 0x20u) {
    if (error) error->clear();
    return true;
  }

  const QString backup = path + QStringLiteral(".fluorine-unpatched");
  if (!QFileInfo::exists(backup) && !QFile::copy(path, backup)) {
    if (error) *error = QString("Cannot back up %1").arg(path);
    return false;
  }
  flags |= 0x20u;
  contents[offset] = static_cast<char>(flags & 0xffu);
  contents[offset + 1] = static_cast<char>((flags >> 8) & 0xffu);
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly)
      || output.write(contents) != contents.size() || !output.commit()) {
    if (error) *error = QString("Cannot patch %1").arg(path);
    return false;
  }
  QFile::setPermissions(path, permissions);
  if (!peLargeAddressAware(path)) {
    if (error) *error = QString("Could not verify the 4 GB flag in %1").arg(path);
    return false;
  }
  if (error) error->clear();
  return true;
}

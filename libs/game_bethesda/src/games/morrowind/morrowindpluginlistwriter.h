#ifndef MORROWINDPLUGINLISTWRITER_H
#define MORROWINDPLUGINLISTWRITER_H

#include <QByteArray>
#include <QList>
#include <QString>

namespace MorrowindPluginListWriter
{

enum class Status
{
  Published,
  NoPlugins,
  ReadError,
  InvalidFormat,
  WriteError,
};

struct Result
{
  Status status = Status::Published;
  QString error;
};

[[nodiscard]] Result publish(const QString& path,
                             const QList<QByteArray>& pluginNames);

}  // namespace MorrowindPluginListWriter

#endif  // MORROWINDPLUGINLISTWRITER_H

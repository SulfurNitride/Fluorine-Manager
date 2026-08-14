#ifndef PROFILEMODLISTRENAME_H
#define PROFILEMODLISTRENAME_H

#include <QString>

namespace ProfileModlistRename
{

enum class Status
{
  NoChange,
  Changed,
  ReadError,
  WriteError,
};

struct Result
{
  Status status = Status::NoChange;
  int renamed   = 0;
  QString error;
};

Result apply(const QString& path, const QString& oldName, const QString& newName);

}  // namespace ProfileModlistRename

#endif  // PROFILEMODLISTRENAME_H

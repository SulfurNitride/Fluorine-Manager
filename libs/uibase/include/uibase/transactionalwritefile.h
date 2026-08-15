/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef TRANSACTIONALWRITEFILE_H
#define TRANSACTIONALWRITEFILE_H

#include "dllimport.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QFileDevice>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace MOBase
{

/**
 * Publishes one completely serialized file without exposing partial contents.
 *
 * The constructor inspects, but never opens or changes, the destination.
 * replaceWith() writes an adjacent temporary file and atomically replaces the
 * selected leaf only after all bytes have been written and the complete leaf
 * generation has been revalidated. A strict same-directory case-only symlink
 * alias is preserved for Bethesda profile compatibility; other symlinks and
 * non-regular leaves are refused. Replacing a hardlink detaches only the
 * selected directory entry.
 */
class QDLLEXPORT TransactionalWriteFile final
{
public:
  explicit TransactionalWriteFile(QString fileName);
  ~TransactionalWriteFile() noexcept;

  Q_DISABLE_COPY_MOVE(TransactionalWriteFile)

  [[nodiscard]] bool readOriginal(QByteArray& contents, bool& present);
  [[nodiscard]] bool readPermissions(QFileDevice::Permissions& permissions);
  [[nodiscard]] bool setPermissions(QFileDevice::Permissions permissions);
  [[nodiscard]] bool setModificationTime(const QDateTime& modificationTime);
  [[nodiscard]] bool replaceWith(QByteArrayView contents);
  [[nodiscard]] QString errorString() const;

private:
  class Impl;
  std::unique_ptr<Impl> d;
};

} // namespace MOBase

#endif // TRANSACTIONALWRITEFILE_H

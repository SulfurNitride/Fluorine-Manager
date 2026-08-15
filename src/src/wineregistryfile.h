/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef WINEREGISTRYFILE_H
#define WINEREGISTRYFILE_H

#include <QList>
#include <QString>
#include <QStringList>

namespace WineRegistryFile
{

struct Query
{
  QString section;
  QString name;
  bool present{false};
  QString value;
};

struct Update
{
  QString section;
  QString name;
  QString value;
  bool compareExisting{false};
  bool expectedPresent{false};
  QString expectedValue;
};

enum class Status
{
  Success,
  Conflict,
  Failure,
};

struct Result
{
  Status status{Status::Failure};
  QString error;
  bool changed{false};

  explicit operator bool() const noexcept { return status == Status::Success; }
};

Result readValues(const QString& path, QList<Query>& queries);
Result updateValues(const QString& path, const QList<Update>& updates);
Result removeDriveMappings(const QString& path, QStringList& removed);

} // namespace WineRegistryFile

#endif // WINEREGISTRYFILE_H

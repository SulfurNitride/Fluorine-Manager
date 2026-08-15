/*
Copyright (C) 2026 Fluorine Manager contributors.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef WINESAVEROUTING_H
#define WINESAVEROUTING_H

#include <QByteArray>
#include <QString>

namespace WineSaveRouting {

struct Result {
  bool success{false};
  QString error;
  // True only after the durable receipt exists. Callers must retain cleanup
  // ownership even when the subsequent INI publication fails.
  bool recoveryRequired{false};

  explicit operator bool() const noexcept { return success; }
};

struct Value {
  bool present{false};
  QByteArray bytes;
};

// Reads a Bethesda-style INI without interpreting backslashes. Section and
// key matching are case-insensitive; the bytes after the first '=' are
// returned exactly (excluding the line ending).
Result readValue(const QString &iniPath, const QByteArray &section,
                 const QByteArray &key, Value &value);

QString receiptPathFor(const QString &iniPath);
Result pendingOwner(const QString &iniPath, QString &ownerId, bool &present);

// Atomically records the prior values and then atomically publishes both
// routing keys. No QSettings escape/continuation semantics are involved.
Result activate(const QString &iniPath, const QString &ownerId);

// Restores both prior values from the durable receipt. An empty expectedOwner
// is reserved for an explicit recovery path; normal teardown authenticates the
// launch owner. The receipt is removed only after the INI commit succeeds.
Result restore(const QString &iniPath, const QString &expectedOwner);

} // namespace WineSaveRouting

#endif // WINESAVEROUTING_H

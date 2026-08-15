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

// Resolves the route that the game should read relative to its INI. An
// explicit route is used for games with a stable lexical contract (notably
// Enderal); otherwise the route is derived from the authenticated save target.
// Both the INI and save target must remain inside allowedRoot.
Result routeFor(const QString &iniPath, const QString &saveDirectory,
                const QString &allowedRoot, const QByteArray &explicitRoute,
                QByteArray &route);

// Reports whether a configured relative route resolves to saveDirectory.
// Absolute values are outside Fluorine's managed relative-route contract and
// are a non-match. Malformed relative routes and root escapes fail closed.
Result routeTargetsDirectory(const QString &iniPath, const QByteArray &route,
                             const QString &saveDirectory,
                             const QString &allowedRoot, bool &matches);

// Inspects every effective case variant that activate() would mutate. Unsafe
// aliases or malformed INIs fail the inspection instead of being skipped.
Result familyTargetsDirectory(const QString &iniPath,
                              const QString &saveDirectory,
                              const QString &allowedRoot, bool &matches);

// User-confirmed recovery for the pre-transactional profile/savepath.ini
// format. The legacy receipt has no target identity, so callers must select
// and display the target explicitly. Only that exact effective target is
// restored, and the receipt is retained until its atomic update succeeds.
Result restoreConfirmedLegacyReceipt(const QString &iniPath,
                                     const QString &receiptPath,
                                     const QString &routeContextIni,
                                     const QByteArray &managedRoute,
                                     const QString &saveDirectory,
                                     const QString &allowedRoot);

// User-confirmed recovery for the legacy case where the original keys were
// absent and no savepath.ini was created. Only an exact managed pair in the
// selected INI is removed; distinct managed case siblings fail closed.
Result clearConfirmedLegacyRoute(const QString &iniPath,
                                 const QString &routeContextIni,
                                 const QByteArray &managedRoute,
                                 const QString &saveDirectory,
                                 const QString &allowedRoot);

QString receiptPathFor(const QString &iniPath);
Result pendingOwner(const QString &iniPath, QString &ownerId, bool &present);

// Atomically records the prior values and then atomically publishes both
// routing keys. No QSettings escape/continuation semantics are involved.
Result activate(const QString &iniPath, const QString &ownerId,
                const QByteArray &route, const QString &saveDirectory,
                const QString &allowedRoot);

// Restores both prior values from the durable receipt. An empty expectedOwner
// is reserved for an explicit recovery path; normal teardown authenticates the
// launch owner. The receipt is removed only after the INI commit succeeds.
Result restore(const QString &iniPath, const QString &expectedOwner);

} // namespace WineSaveRouting

#endif // WINESAVEROUTING_H

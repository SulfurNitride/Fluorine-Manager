#pragma once

#include <QString>

// Resolve aliases outside the mount itself (for example Fedora's /home ->
// /var/home) while still working when the mount target is stale.
QString mountPathIdentity(const QString& path);

bool mountPathsEquivalent(const QString& left, const QString& right);

bool mountTableContainsPath(const QString& mountTable, const QString& path);

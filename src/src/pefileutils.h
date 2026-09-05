#pragma once

#include <QString>

// Reads and updates the IMAGE_FILE_LARGE_ADDRESS_AWARE characteristic without
// executing an untrusted Windows patcher. The mutating operation preserves a
// one-time .fluorine-unpatched backup and verifies the written PE header.
bool peLargeAddressAware(const QString& path);
bool setPeLargeAddressAware(const QString& path, QString* error = nullptr);

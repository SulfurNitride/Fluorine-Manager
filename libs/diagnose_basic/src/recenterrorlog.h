#pragma once

#include <QString>

#include <optional>

namespace diagnose_basic
{

// Returns escaped HTML containing the most recent error and nearby lines from
// one interface log. The scan is deliberately bounded for diagnosis checks
// that run during normal UI refreshes.
std::optional<QString> recentErrorContext(const QString& logPath);

}  // namespace diagnose_basic

#ifndef LEGACYFLATPAKMIGRATION_H
#define LEGACYFLATPAKMIGRATION_H

#include <QString>
#include <QStringList>

namespace LegacyFlatpakMigration
{
enum class Status
{
  NotNeeded,
  Complete,
  Attention,
  Failed,
};

struct Paths
{
  QString legacyRoot;
  QString dataRoot;
  QString configRoot;
  QString legacyProcessKey;
  int lockTimeoutMs = 30000;
};

struct Result
{
  Status status = Status::NotNeeded;
  QStringList diagnostics;
  QString attentionReport;
};

Paths defaultPaths();

// Imports the fixed layouts used by historical Flatpak releases. Existing
// native destinations always win; ambiguous data is retained and reported.
// The operation is restartable and never recursively copies a Wine prefix.
Result migrate(const Paths& paths);

QString completionMarkerPath(const Paths& paths);
QString prefixReceiptPath(const Paths& paths);

}  // namespace LegacyFlatpakMigration

#endif  // LEGACYFLATPAKMIGRATION_H

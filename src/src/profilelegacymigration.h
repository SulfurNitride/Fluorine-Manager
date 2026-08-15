#pragma once

#include <QSettings>
#include <QString>
#include <QVariant>

#include <functional>

namespace ProfileLegacyMigration {

enum class EntryKind {
  File,
  Directory,
};

enum class Status {
  Completed,
  InvalidRequest,
  SettingsUnavailable,
  SourceUnavailable,
  DestinationExists,
  UnsafeEntry,
  RenameFailed,
  PublicationUncertain,
};

struct Result {
  Status status{Status::InvalidRequest};
  QString error;

  [[nodiscard]] bool succeeded() const noexcept {
    return status == Status::Completed;
  }
};

struct Hooks {
  std::function<void()> afterIntentPersisted;
  std::function<void()> afterRenamePublished;
};

// Returns the operation stored in a durable migration intent, or an empty
// string when no migration is pending. Malformed intents are returned as an
// invalid non-empty marker so callers fail closed instead of choosing a new
// migration direction.
[[nodiscard]] QString pendingOperation(const QSettings &settings,
                                       const QString &intentKey);

// Moves one immediate child of profileRoot without replacing another entry.
// A durable intent containing the source identity is published before the
// rename. On retry, the same identity may be found at either source or
// destination; only then is the final setting committed and the intent
// removed.
[[nodiscard]] Result migrate(QSettings &settings, const QString &profileRoot,
                             const QString &intentKey, const QString &operation,
                             const QString &sourceLeaf,
                             const QString &destinationLeaf, EntryKind kind,
                             const QString &finalSettingKey,
                             const QVariant &finalSettingValue,
                             const Hooks *hooks = nullptr);

} // namespace ProfileLegacyMigration

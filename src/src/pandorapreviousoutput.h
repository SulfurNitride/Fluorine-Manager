#pragma once

#include <QString>

namespace PandoraPreviousOutput {

enum class Status {
  Missing,
  Unchanged,
  Updated,
  Failed,
};

struct Result {
  Status status{Status::Failed};
  QString error;

  explicit operator bool() const noexcept { return status != Status::Failed; }
};

// Lowercases each path suffix beginning at "\meshes\" while preserving the
// file's exact line endings and final-newline state. The live file is replaced
// only after the complete transformed generation has been serialized.
[[nodiscard]] Result normalize(const QString &path);

} // namespace PandoraPreviousOutput

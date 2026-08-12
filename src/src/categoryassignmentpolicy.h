#ifndef CATEGORYASSIGNMENTPOLICY_H
#define CATEGORYASSIGNMENTPOLICY_H

#include <QString>

#include <algorithm>
#include <limits>
#include <optional>
#include <set>

namespace CategoryAssignmentPolicy
{
// Imported IDs occupy a deterministic namespace keyed by Nexus identity. An
// old local ID can therefore survive a replacement only when it still denotes
// the same Nexus category. Retired local IDs are tracked by a durable high-water
// mark so stale metadata can never reinterpret them as an unrelated category.
inline constexpr int ImportedCategoryIdBase = 1'000'000;

inline std::optional<int> importedCategoryId(int nexusId)
{
  if (nexusId <= 0 ||
      nexusId > std::numeric_limits<int>::max() - ImportedCategoryIdBase) {
    return std::nullopt;
  }
  return ImportedCategoryIdBase + nexusId;
}

inline bool isSafeSerializedName(const QString& name)
{
  if (name.trimmed().isEmpty() || name.contains(QLatin1Char('|'))) {
    return false;
  }
  for (const QChar character : name) {
    if (character.category() == QChar::Other_Control) {
      return false;
    }
  }
  return true;
}

inline std::optional<int> nextLocalCategoryId(int durableHighWater,
                                              const std::set<int>& usedIds)
{
  int candidate = std::max(0, durableHighWater);
  do {
    if (candidate >= ImportedCategoryIdBase - 1) {
      return std::nullopt;
    }
    ++candidate;
  } while (usedIds.contains(candidate));
  return candidate;
}

inline bool mayAssignCategoryId(int durableHighWater, int requestedId,
                                std::optional<int> originalId)
{
  if (originalId && *originalId == requestedId) {
    return true;
  }
  return requestedId > durableHighWater && requestedId > 0 &&
         requestedId < ImportedCategoryIdBase;
}
}  // namespace CategoryAssignmentPolicy

#endif  // CATEGORYASSIGNMENTPOLICY_H

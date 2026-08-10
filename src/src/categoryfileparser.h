#ifndef CATEGORYFILEPARSER_H
#define CATEGORYFILEPARSER_H

#include "categoryassignmentpolicy.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace CategoryFileParser
{
inline constexpr qsizetype MaximumInputBytes = 4 * 1024 * 1024;
inline constexpr int MaximumCategoryRecords  = 100'000;
inline constexpr int MaximumParentDepth      = 1'024;

struct CategoryRecord
{
  int id{0};
  QString name;
  std::vector<int> nexusIds;
  int parentId{0};
};

struct NexusRecord
{
  int categoryId{0};
  QString name;
  int nexusId{0};
};

struct Inventory
{
  std::vector<CategoryRecord> categories;
  std::vector<NexusRecord> nexusMappings;
};

struct Result
{
  std::optional<Inventory> inventory;
  QString error;

  explicit operator bool() const { return inventory.has_value(); }
};

inline bool parseInteger(const QByteArray& value, int& result)
{
  bool ok = false;
  result  = value.trimmed().toInt(&ok, 10);
  return ok;
}

inline QString decodeName(const QByteArray& value)
{
  const QString name = QString::fromUtf8(value);
  return name.contains(QChar::ReplacementCharacter) ? QString() : name;
}

inline Result fail(const QString& message, int line)
{
  return {std::nullopt, QStringLiteral("%1 at line %2").arg(message).arg(line)};
}

inline Result parse(const QByteArray& categoriesData,
                    const QByteArray& nexusMapData)
{
  if (categoriesData.size() > MaximumInputBytes ||
      nexusMapData.size() > MaximumInputBytes) {
    return {std::nullopt, QStringLiteral("category input exceeds size limit")};
  }

  Inventory result;
  std::set<int> categoryIds{0};
  std::set<int> embeddedNexusIds;
  const auto categoryLines = categoriesData.split('\n');
  std::set<int> declaredCategoryIds;
  for (QByteArray line : categoryLines) {
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    const auto cells = line.split('|');
    int id           = 0;
    if ((cells.size() == 3 || cells.size() == 4) &&
        parseInteger(cells[0], id) && id > 0) {
      declaredCategoryIds.insert(id);
    }
  }
  for (int lineNumber = 0; lineNumber < categoryLines.size(); ++lineNumber) {
    QByteArray line = categoryLines[lineNumber];
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    if (line.isEmpty()) {
      continue;
    }
    if (result.categories.size() >= MaximumCategoryRecords) {
      return fail(QStringLiteral("too many category records"), lineNumber + 1);
    }

    const auto cells = line.split('|');
    if (cells.size() != 3 && cells.size() != 4) {
      return fail(QStringLiteral("invalid category field count"),
                  lineNumber + 1);
    }

    CategoryRecord category;
    if (!parseInteger(cells[0], category.id) || category.id <= 0) {
      return fail(QStringLiteral("invalid category ID"), lineNumber + 1);
    }
    category.name = decodeName(cells[1]);
    if (!CategoryAssignmentPolicy::isSafeSerializedName(category.name)) {
      return fail(QStringLiteral("invalid category name"), lineNumber + 1);
    }
    const int parentCell = cells.size() == 4 ? 3 : 2;
    if (!parseInteger(cells[parentCell], category.parentId) ||
        category.parentId < 0 || category.parentId == category.id) {
      return fail(QStringLiteral("invalid parent category ID"),
                  lineNumber + 1);
    }

    if (cells.size() == 4 && !cells[2].isEmpty()) {
      const auto nexusIds = cells[2].split(',');
      for (const auto& nexusValue : nexusIds) {
        int nexusId = 0;
        if (!parseInteger(nexusValue, nexusId) || nexusId <= 0 ||
            !embeddedNexusIds.insert(nexusId).second) {
          return fail(QStringLiteral("invalid or duplicate Nexus ID"),
                      lineNumber + 1);
        }
        category.nexusIds.push_back(nexusId);
      }
    }

    if (!categoryIds.insert(category.id).second) {
      // Historical MO2 defaults accidentally used 39 for both Voice and
      // Tattoos. Preserve the effective Tattoos=39 meaning and migrate Voice
      // to the first unused legacy ID instead of accepting arbitrary
      // duplicates.
      if (category.id == 39 && category.name == QStringLiteral("Tattoos") &&
          !categoryIds.contains(59)) {
        auto voice = std::find_if(
            result.categories.begin(), result.categories.end(),
            [](const CategoryRecord& existing) {
              return existing.id == 39 &&
                     existing.name == QStringLiteral("Voice");
            });
        if (voice == result.categories.end()) {
          return fail(QStringLiteral("duplicate category ID"), lineNumber + 1);
        }
        int migratedVoiceId = 59;
        while (declaredCategoryIds.contains(migratedVoiceId) ||
               categoryIds.contains(migratedVoiceId)) {
          if (migratedVoiceId == std::numeric_limits<int>::max()) {
            return fail(QStringLiteral("cannot migrate duplicate category ID"),
                        lineNumber + 1);
          }
          ++migratedVoiceId;
        }
        voice->id = migratedVoiceId;
        categoryIds.insert(migratedVoiceId);
      } else {
        return fail(QStringLiteral("duplicate category ID"), lineNumber + 1);
      }
    }
    result.categories.push_back(std::move(category));
  }

  std::map<int, int> parents;
  for (const auto& category : result.categories) {
    if (!categoryIds.contains(category.parentId)) {
      return {std::nullopt,
              QStringLiteral("category %1 references missing parent %2")
                  .arg(category.id)
                  .arg(category.parentId)};
    }
    parents.emplace(category.id, category.parentId);
  }
  enum class VisitState : unsigned char
  {
    Unvisited,
    Visiting,
    Complete
  };
  std::map<int, VisitState> visitStates;
  std::map<int, int> parentDepths{{0, 0}};
  for (const auto& [categoryId, unused] : parents) {
    (void)unused;
    if (visitStates[categoryId] == VisitState::Complete) {
      continue;
    }

    std::vector<int> path;
    int current = categoryId;
    int completedDepth = 0;
    while (current != 0) {
      const auto state = visitStates[current];
      if (state == VisitState::Visiting) {
        return {std::nullopt,
                QStringLiteral("category parent cycle involving %1")
                    .arg(categoryId)};
      }
      if (state == VisitState::Complete) {
        completedDepth = parentDepths.at(current);
        break;
      }
      visitStates[current] = VisitState::Visiting;
      path.push_back(current);
      if (path.size() > static_cast<std::size_t>(MaximumParentDepth)) {
        return {std::nullopt,
                QStringLiteral("category parent depth exceeds %1")
                    .arg(MaximumParentDepth)};
      }
      current = parents.at(current);
    }
    for (auto completed = path.rbegin(); completed != path.rend();
         ++completed) {
      ++completedDepth;
      if (completedDepth > MaximumParentDepth) {
        return {std::nullopt,
                QStringLiteral("category parent depth exceeds %1")
                    .arg(MaximumParentDepth)};
      }
      visitStates[*completed] = VisitState::Complete;
      parentDepths[*completed] = completedDepth;
    }
  }

  std::set<int> mappedNexusIds;
  const auto nexusLines = nexusMapData.split('\n');
  for (int lineNumber = 0; lineNumber < nexusLines.size(); ++lineNumber) {
    QByteArray line = nexusLines[lineNumber];
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    if (line.isEmpty()) {
      continue;
    }
    if (result.nexusMappings.size() >= MaximumCategoryRecords) {
      return fail(QStringLiteral("too many Nexus mapping records"),
                  lineNumber + 1);
    }

    const auto cells = line.split('|');
    NexusRecord mapping;
    if (cells.size() != 3 || !parseInteger(cells[0], mapping.categoryId) ||
        (mapping.categoryId != -1 &&
         !categoryIds.contains(mapping.categoryId))) {
      return fail(QStringLiteral("invalid Nexus mapping category ID"),
                  lineNumber + 1);
    }
    mapping.name = decodeName(cells[1]);
    if (!CategoryAssignmentPolicy::isSafeSerializedName(mapping.name) ||
        !parseInteger(cells[2], mapping.nexusId) || mapping.nexusId <= 0 ||
        !mappedNexusIds.insert(mapping.nexusId).second) {
      return fail(QStringLiteral("invalid or duplicate Nexus mapping"),
                  lineNumber + 1);
    }
    result.nexusMappings.push_back(std::move(mapping));
  }

  return {std::move(result), QString()};
}
}  // namespace CategoryFileParser

#endif  // CATEGORYFILEPARSER_H

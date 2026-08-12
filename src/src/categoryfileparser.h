#ifndef CATEGORYFILEPARSER_H
#define CATEGORYFILEPARSER_H

#include <QByteArray>
#include <QString>

#include <optional>
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

Result parse(const QByteArray& categoriesData, const QByteArray& nexusMapData);
}  // namespace CategoryFileParser

#endif  // CATEGORYFILEPARSER_H

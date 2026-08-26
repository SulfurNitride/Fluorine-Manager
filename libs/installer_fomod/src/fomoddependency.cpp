#include "fomoddependency.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace FomodDependency
{
namespace
{

constexpr int TypeRequired      = 0;
constexpr int TypeRecommended   = 1;
constexpr int TypeOptional      = 2;
constexpr int TypeNotUsable     = 3;
constexpr int TypeCouldBeUsable = 4;

int typeRank(int type)
{
  switch (type) {
  case TypeRequired:
    return 4;
  case TypeRecommended:
    return 3;
  case TypeOptional:
    return 2;
  case TypeCouldBeUsable:
    return 1;
  case TypeNotUsable:
    return 0;
  default:
    return 2;
  }
}

QString typeName(int type)
{
  switch (type) {
  case TypeRequired:
    return QStringLiteral("required");
  case TypeRecommended:
    return QStringLiteral("recommended");
  case TypeOptional:
    return QStringLiteral("optional");
  case TypeCouldBeUsable:
    return QStringLiteral("potentially usable");
  case TypeNotUsable:
    return QStringLiteral("not usable");
  default:
    return QStringLiteral("changed");
  }
}

bool conditionMatches(const QJsonObject& condition,
                      const FileStateResolver& resolver)
{
  const QString kind = condition.value(QStringLiteral("kind")).toString();
  if (kind == QLatin1String("constant")) {
    return condition.value(QStringLiteral("value")).toBool();
  }
  if (kind == QLatin1String("file")) {
    const QString file     = condition.value(QStringLiteral("file")).toString();
    const QString expected = condition.value(QStringLiteral("state"))
                                 .toString()
                                 .trimmed()
                                 .toLower();
    const FileState actual = resolver(file);
    if (expected == QLatin1String("active")) {
      return actual == FileState::Active;
    }
    if (expected == QLatin1String("inactive")) {
      return actual == FileState::Inactive;
    }
    return actual == FileState::Missing;
  }
  if (kind == QLatin1String("and") || kind == QLatin1String("or")) {
    const QJsonArray children = condition.value(QStringLiteral("children")).toArray();
    if (children.isEmpty()) {
      return true;
    }
    if (kind == QLatin1String("or")) {
      for (const auto& child : children) {
        if (conditionMatches(child.toObject(), resolver)) {
          return true;
        }
      }
      return false;
    }
    for (const auto& child : children) {
      if (!conditionMatches(child.toObject(), resolver)) {
        return false;
      }
    }
    return true;
  }

  // Unknown nodes are frozen by the encoder as constants. Be conservative if a
  // newer schema reaches an older evaluator.
  return true;
}

int effectiveType(const QJsonObject& record, const FileStateResolver& resolver)
{
  const QJsonArray patterns = record.value(QStringLiteral("patterns")).toArray();
  for (const auto& value : patterns) {
    const QJsonObject pattern = value.toObject();
    if (conditionMatches(pattern.value(QStringLiteral("condition")).toObject(),
                         resolver)) {
      return pattern.value(QStringLiteral("type")).toInt(TypeOptional);
    }
  }
  return record.value(QStringLiteral("defaultType")).toInt(TypeOptional);
}

QJsonDocument parseSnapshot(const QByteArray& bytes)
{
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }
  return document;
}

}  // namespace

bool conditionUsesFile(const QJsonObject& condition)
{
  const QString kind = condition.value(QStringLiteral("kind")).toString();
  if (kind == QLatin1String("file")) {
    return true;
  }
  for (const auto& child : condition.value(QStringLiteral("children")).toArray()) {
    if (conditionUsesFile(child.toObject())) {
      return true;
    }
  }
  return false;
}

QStringList reviewReasons(const QByteArray& snapshot,
                          const FileStateResolver& resolver)
{
  const QJsonDocument document = parseSnapshot(snapshot);
  if (document.isNull() || !document.object().value(QStringLiteral("baselined")).toBool()) {
    return {};
  }

  QStringList reasons;
  const QJsonArray records = document.object().value(QStringLiteral("records")).toArray();
  for (const auto& value : records) {
    const QJsonObject record = value.toObject();
    const QString kind       = record.value(QStringLiteral("kind")).toString();
    const QString label      = record.value(QStringLiteral("label")).toString();
    if (kind == QLatin1String("option")) {
      // Every choice is retained so a reinstall can restore it, but only choices
      // whose type actually depends on an external file can request a review.
      if (!record.value(QStringLiteral("dependencyDriven")).toBool(true)) {
        continue;
      }
      const int baseline = record.value(QStringLiteral("baselineType"))
                               .toInt(TypeOptional);
      const int current  = effectiveType(record, resolver);
      const bool selected = record.value(QStringLiteral("selected")).toBool();

      if (!selected && baseline == TypeNotUsable && current != TypeNotUsable) {
        reasons.append(QStringLiteral("%1 is now available").arg(label));
      } else if (!selected && typeRank(current) > typeRank(baseline) &&
                 typeRank(current) >= typeRank(TypeRecommended)) {
        reasons.append(
            QStringLiteral("%1 is now %2").arg(label, typeName(current)));
      } else if (selected && baseline != TypeNotUsable &&
                 current == TypeNotUsable) {
        reasons.append(QStringLiteral("%1 is no longer usable").arg(label));
      }
    } else if (kind == QLatin1String("predicate")) {
      const bool baseline = record.value(QStringLiteral("baseline")).toBool();
      const bool current  = conditionMatches(
          record.value(QStringLiteral("condition")).toObject(), resolver);
      if (baseline != current) {
        reasons.append(current ? QStringLiteral("%1 now applies").arg(label)
                               : QStringLiteral("%1 no longer applies").arg(label));
      }
    }
  }
  reasons.removeDuplicates();
  return reasons;
}

bool isBaselined(const QByteArray& snapshot)
{
  const QJsonDocument document = parseSnapshot(snapshot);
  return !document.isNull() &&
         document.object().value(QStringLiteral("baselined")).toBool();
}

QByteArray rebaseline(const QByteArray& snapshot, const FileStateResolver& resolver)
{
  QJsonDocument document = parseSnapshot(snapshot);
  if (document.isNull()) {
    return {};
  }

  QJsonObject root   = document.object();
  QJsonArray records = root.value(QStringLiteral("records")).toArray();
  for (qsizetype i = 0; i < records.size(); ++i) {
    QJsonObject record = records.at(i).toObject();
    if (record.value(QStringLiteral("kind")).toString() == QLatin1String("option")) {
      record.insert(QStringLiteral("baselineType"), effectiveType(record, resolver));
    } else if (record.value(QStringLiteral("kind")).toString() ==
               QLatin1String("predicate")) {
      record.insert(
          QStringLiteral("baseline"),
          conditionMatches(record.value(QStringLiteral("condition")).toObject(),
                           resolver));
    }
    records.replace(i, record);
  }
  root.insert(QStringLiteral("records"), records);
  root.insert(QStringLiteral("baselined"), true);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

}  // namespace FomodDependency

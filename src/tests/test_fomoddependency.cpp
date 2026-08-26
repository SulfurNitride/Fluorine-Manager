#include <gtest/gtest.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "fomoddependency.h"

namespace
{

QJsonObject fileCondition(const QString& file, const QString& state = "Active")
{
  return {{"kind", "file"}, {"file", file}, {"state", state}};
}

QByteArray optionSnapshot(bool selected, int defaultType, int patternType,
                          bool baselined = false)
{
  const QJsonObject option{
      {"kind", "option"},
      {"label", "Compatibility › Example patch"},
      {"selected", selected},
      {"baselineType", defaultType},
      {"defaultType", defaultType},
      {"patterns",
       QJsonArray{QJsonObject{{"type", patternType},
                              {"condition", fileCondition("Example.esp")}}}}};
  return QJsonDocument(QJsonObject{{"schema", 1},
                                   {"baselined", baselined},
                                   {"records", QJsonArray{option}}})
      .toJson(QJsonDocument::Compact);
}

struct States
{
  QHash<QString, FomodDependency::FileState> values;

  FomodDependency::FileState operator()(const QString& file) const
  {
    return values.value(file.toLower(), FomodDependency::FileState::Missing);
  }
};

}  // namespace

TEST(FomodDependency, NewRecommendedOptionTriggersAfterBaseline)
{
  States states;
  QByteArray snapshot = optionSnapshot(false, 3, 1);  // NotUsable -> Recommended.

  EXPECT_FALSE(FomodDependency::isBaselined(snapshot));
  EXPECT_TRUE(FomodDependency::reviewReasons(snapshot, states).isEmpty());

  snapshot = FomodDependency::rebaseline(snapshot, states);
  EXPECT_TRUE(FomodDependency::isBaselined(snapshot));
  EXPECT_TRUE(FomodDependency::reviewReasons(snapshot, states).isEmpty());

  states.values.insert("example.esp", FomodDependency::FileState::Active);
  const QStringList reasons = FomodDependency::reviewReasons(snapshot, states);
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_TRUE(reasons.front().contains("now available"));
}

TEST(FomodDependency, AlreadyApplicableDeclinedOptionDoesNotNag)
{
  States states;
  states.values.insert("example.esp", FomodDependency::FileState::Active);
  QByteArray snapshot = FomodDependency::rebaseline(
      optionSnapshot(false, 3, 1), states);

  EXPECT_TRUE(FomodDependency::reviewReasons(snapshot, states).isEmpty());
  states.values.remove("example.esp");
  EXPECT_TRUE(FomodDependency::reviewReasons(snapshot, states).isEmpty());
  states.values.insert("example.esp", FomodDependency::FileState::Active);
  EXPECT_TRUE(FomodDependency::reviewReasons(snapshot, states).isEmpty());
}

TEST(FomodDependency, PlainTrackedChoiceNeverRequestsDependencyReview)
{
  States states;
  QJsonDocument document = QJsonDocument::fromJson(
      FomodDependency::rebaseline(optionSnapshot(false, 3, 1), states));
  QJsonObject root = document.object();
  QJsonArray records = root.value("records").toArray();
  QJsonObject option = records.at(0).toObject();
  option.insert("dependencyDriven", false);
  records.replace(0, option);
  root.insert("records", records);

  states.values.insert("example.esp", FomodDependency::FileState::Active);
  EXPECT_TRUE(FomodDependency::reviewReasons(
                  QJsonDocument(root).toJson(QJsonDocument::Compact), states)
                  .isEmpty());
}

TEST(FomodDependency, OptionalOptionBecomingRecommendedTriggers)
{
  States states;
  QByteArray snapshot = FomodDependency::rebaseline(
      optionSnapshot(false, 2, 1), states);  // Optional -> Recommended.

  states.values.insert("example.esp", FomodDependency::FileState::Active);
  const QStringList reasons = FomodDependency::reviewReasons(snapshot, states);
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_TRUE(reasons.front().contains("now recommended"));
}

TEST(FomodDependency, SelectedPatchTriggersWhenItBecomesUnusable)
{
  States states;
  states.values.insert("example.esp", FomodDependency::FileState::Active);
  QByteArray snapshot = FomodDependency::rebaseline(
      optionSnapshot(true, 3, 1), states);

  states.values.remove("example.esp");
  const QStringList reasons = FomodDependency::reviewReasons(snapshot, states);
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_TRUE(reasons.front().contains("no longer usable"));
}

TEST(FomodDependency, ConditionalFilePredicateTracksInactiveState)
{
  const QJsonObject predicate{
      {"kind", "predicate"},
      {"label", "Conditional file set 1"},
      {"baseline", false},
      {"condition", fileCondition("Example.esp", "Inactive")}};
  QByteArray snapshot = QJsonDocument(
      QJsonObject{{"schema", 1},
                  {"baselined", false},
                  {"records", QJsonArray{predicate}}})
                            .toJson(QJsonDocument::Compact);
  States states;
  snapshot = FomodDependency::rebaseline(snapshot, states);

  states.values.insert("example.esp", FomodDependency::FileState::Inactive);
  const QStringList reasons = FomodDependency::reviewReasons(snapshot, states);
  ASSERT_EQ(reasons.size(), 1);
  EXPECT_TRUE(reasons.front().contains("now applies"));
}

TEST(FomodDependency, CompositeConditionsPreserveAndOrSemantics)
{
  const QJsonObject condition{
      {"kind", "or"},
      {"children",
       QJsonArray{
           QJsonObject{{"kind", "and"},
                       {"children",
                        QJsonArray{fileCondition("A.esp"),
                                   fileCondition("B.esp")}}},
           fileCondition("Alternative.esp")}}};
  EXPECT_TRUE(FomodDependency::conditionUsesFile(condition));

  const QJsonObject predicate{{"kind", "predicate"},
                              {"label", "Conditional file set"},
                              {"baseline", false},
                              {"condition", condition}};
  const QByteArray raw =
      QJsonDocument(QJsonObject{{"schema", 1},
                                {"baselined", true},
                                {"records", QJsonArray{predicate}}})
          .toJson(QJsonDocument::Compact);
  States states;
  states.values.insert("a.esp", FomodDependency::FileState::Active);
  EXPECT_TRUE(FomodDependency::reviewReasons(raw, states).isEmpty());
  states.values.insert("b.esp", FomodDependency::FileState::Active);
  EXPECT_EQ(FomodDependency::reviewReasons(raw, states).size(), 1);
}

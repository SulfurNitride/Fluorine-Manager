#include <gtest/gtest.h>

#include "plugincompatibility.h"

#include <QPointer>

#include <stdexcept>

namespace
{

struct FakePlugin
{
  QString name;
  FakePlugin* master{nullptr};
};

std::optional<PluginCompatibility::Block> blocked(FakePlugin* plugin)
{
  return PluginCompatibility::blockedRuleForPlugin(
      QStringLiteral("Morrowind (OpenMW)"), plugin,
      [](FakePlugin* current) { return current->name; },
      [](FakePlugin* current) { return current->master; });
}

}  // namespace

TEST(PluginCompatibility, BlocksOpenMWPlayerForNativeOpenMW)
{
  const auto block = PluginCompatibility::blockedRule(
      QStringLiteral("Morrowind (OpenMW)"), {QStringLiteral("OpenMWPlayer")});

  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(block->id, QStringLiteral("openmwplayer-native-openmw"));
}

TEST(PluginCompatibility, BlocksDescendantsByMasterAncestry)
{
  EXPECT_TRUE(PluginCompatibility::blockedRule(
                  QStringLiteral("Morrowind (OpenMW)"),
                  {QStringLiteral("OpenMWPlayer Launcher"),
                   QStringLiteral("OpenMWPlayer")})
                  .has_value());
}

TEST(PluginCompatibility, TraversesMasterAncestryAndStopsAtCycles)
{
  FakePlugin root{QStringLiteral("OpenMWPlayer")};
  FakePlugin child{QStringLiteral("OpenMWPlayer Launcher"), &root};
  FakePlugin grandchild{QStringLiteral("OpenMWPlayer Child Tool"), &child};

  EXPECT_TRUE(blocked(&grandchild).has_value());

  root.master = &grandchild;
  EXPECT_TRUE(blocked(&grandchild).has_value());
}

TEST(PluginCompatibility, AllowsClassicMorrowindAndOtherPlugins)
{
  EXPECT_FALSE(PluginCompatibility::blockedRule(
                   QStringLiteral("Morrowind"), {QStringLiteral("OpenMWPlayer")})
                   .has_value());
  EXPECT_FALSE(PluginCompatibility::blockedRule(
                   QStringLiteral("Morrowind (OpenMW)"),
                   {QStringLiteral("Unrelated Plugin")})
                   .has_value());
  EXPECT_TRUE(PluginCompatibility::blockedRule(
                  QStringLiteral("morrowind (openmw)"),
                  {QStringLiteral("OpenMWPlayer")})
                  .has_value());
}

TEST(PluginCompatibility, SessionOverrideAllowsBlockedRule)
{
  EXPECT_FALSE(PluginCompatibility::blockedRule(
                   QStringLiteral("Morrowind (OpenMW)"),
                   {QStringLiteral("OpenMWPlayer")},
                   {QStringLiteral("openmwplayer-native-openmw")})
                   .has_value());
}

TEST(PluginCompatibility, ReadsSessionOverridesFromEnvironment)
{
  const auto variable = QByteArrayLiteral("FLUORINE_ALLOW_INCOMPATIBLE_PLUGINS");
  const auto original = qgetenv(variable.constData());
  qputenv(variable.constData(),
          QByteArrayLiteral("other-rule, openmwplayer-native-openmw"));

  const auto overrides = PluginCompatibility::environmentOverrides();

  if (original.isNull()) {
    qunsetenv(variable.constData());
  } else {
    qputenv(variable.constData(), original);
  }
  EXPECT_TRUE(overrides.contains(QStringLiteral("other-rule")));
  EXPECT_TRUE(overrides.contains(QStringLiteral("openmwplayer-native-openmw")));
}

TEST(PluginCompatibility, PreInitPolicyBlocksRootAndUnregisteredDescendants)
{
  PluginCompatibility::RegistrationPolicy policy(
      QStringLiteral("Morrowind (OpenMW)"));

  EXPECT_TRUE(policy.block(QStringLiteral("OpenMWPlayer")).has_value());
  EXPECT_TRUE(policy
                  .block(QStringLiteral("OpenMWPlayer Launcher"),
                         QStringLiteral("OpenMWPlayer"))
                  .has_value());
  EXPECT_TRUE(policy
                  .block(QStringLiteral("OpenMWPlayer Child Tool"),
                         QStringLiteral("OpenMWPlayer Launcher"))
                  .has_value());
}

TEST(PluginCompatibility, PreInitPolicyAllowsOtherGamesUnknownAndOverrides)
{
  PluginCompatibility::RegistrationPolicy classic(
      QStringLiteral("Morrowind"));
  EXPECT_FALSE(classic.block(QStringLiteral("OpenMWPlayer")).has_value());

  PluginCompatibility::RegistrationPolicy unknown;
  EXPECT_FALSE(unknown.block(QStringLiteral("OpenMWPlayer")).has_value());

  PluginCompatibility::RegistrationPolicy overridden(
      QStringLiteral("Morrowind (OpenMW)"),
      {QStringLiteral("openmwplayer-native-openmw")});
  EXPECT_FALSE(overridden.block(QStringLiteral("OpenMWPlayer")).has_value());
}

TEST(PluginCompatibility, RequestsMasterOnlyForAnActiveNativeOpenMWRule)
{
  PluginCompatibility::RegistrationPolicy native(
      QStringLiteral("Morrowind (OpenMW)"));
  EXPECT_TRUE(native.needsMasterMetadata());

  PluginCompatibility::RegistrationPolicy classic(
      QStringLiteral("Morrowind"));
  EXPECT_FALSE(classic.needsMasterMetadata());

  PluginCompatibility::RegistrationPolicy unknown;
  EXPECT_FALSE(unknown.needsMasterMetadata());

  PluginCompatibility::RegistrationPolicy overridden(
      QStringLiteral("Morrowind (OpenMW)"),
      {QStringLiteral("openmwplayer-native-openmw")});
  EXPECT_FALSE(overridden.needsMasterMetadata());
}

TEST(PluginCompatibility, DetectsResolvedGamePolicyChanges)
{
  PluginCompatibility::RegistrationPolicy native(
      QStringLiteral("Morrowind (OpenMW)"));
  EXPECT_TRUE(native.matchesGame(QStringLiteral("morrowind (openmw)")));
  EXPECT_FALSE(native.matchesGame(QStringLiteral("Morrowind")));

  PluginCompatibility::RegistrationPolicy classic(
      QStringLiteral("Morrowind"));
  EXPECT_TRUE(classic.matchesGame(QStringLiteral("Skyrim")));
  EXPECT_FALSE(classic.matchesGame(QStringLiteral("Morrowind (OpenMW)")));
}

TEST(PluginCompatibility, UnloadsBeforeRetiringRejectedProxyBatch)
{
  auto* first = new QObject;
  auto* second = new QObject;
  QPointer<QObject> firstGuard(first);
  QPointer<QObject> secondGuard(second);
  bool unloadSawLiveObjects = false;

  PluginCompatibility::retireRejectedProxiedBatch(
      {first, second}, [&] {
        unloadSawLiveObjects = !firstGuard.isNull() && !secondGuard.isNull();
      });

  EXPECT_TRUE(unloadSawLiveObjects);
  EXPECT_TRUE(firstGuard.isNull());
  EXPECT_TRUE(secondGuard.isNull());
}

TEST(PluginCompatibility, PreservesRejectedProxyBatchWhenUnloadThrows)
{
  auto* object = new QObject;
  QPointer<QObject> guard(object);

  EXPECT_THROW(PluginCompatibility::retireRejectedProxiedBatch(
                   {object}, [] { throw std::runtime_error("unload failed"); }),
               std::runtime_error);
  EXPECT_FALSE(guard.isNull());

  delete object;
  EXPECT_TRUE(guard.isNull());
}

TEST(PluginCompatibility, DoesNotDeleteWrappersAlreadyRetiredByProxy)
{
  auto* object = new QObject;
  QPointer<QObject> guard(object);

  EXPECT_NO_THROW(PluginCompatibility::retireRejectedProxiedBatch(
      {object}, [object] { delete object; }));
  EXPECT_TRUE(guard.isNull());
}

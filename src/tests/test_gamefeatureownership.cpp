#include "gamefeatureownership.h"

#include <gameplugins.h>

#include <gtest/gtest.h>

namespace
{
class TestGamePlugins final : public MOBase::GamePlugins
{
public:
  void writePluginLists(const MOBase::IPluginList*) override {}
  void readPluginLists(MOBase::IPluginList*) override {}
  QStringList getLoadOrder() override { return {}; }
};

class ForeignGamePluginsProxy final : public MOBase::GamePlugins,
                                      public ProxiedGameFeature
{
public:
  explicit ForeignGamePluginsProxy(
      std::shared_ptr<MOBase::GamePlugins> proxied)
      : m_Proxied(std::move(proxied))
  {}

  void writePluginLists(const MOBase::IPluginList* plugins) override
  {
    m_Proxied->writePluginLists(plugins);
  }
  void readPluginLists(MOBase::IPluginList* plugins) override
  {
    m_Proxied->readPluginLists(plugins);
  }
  QStringList getLoadOrder() override { return m_Proxied->getLoadOrder(); }

  std::shared_ptr<MOBase::GameFeature> proxiedFeature() const override
  {
    return m_Proxied;
  }

private:
  std::shared_ptr<MOBase::GamePlugins> m_Proxied;
};

struct PluginToken
{};

MOBase::IPlugin* pluginPointer(PluginToken& token)
{
  // RegistrationStore treats plugin pointers only as opaque owner identities.
  return reinterpret_cast<MOBase::IPlugin*>(&token);
}

bool registerThroughFacade(
    game_feature_ownership::RegistrationStore& registrations,
    MOBase::IPlugin* caller, const std::shared_ptr<MOBase::GameFeature>& feature)
{
  return game_feature_ownership::acceptsCallerSuppliedFeature(feature) &&
         registrations.registerFeature(caller, {}, feature, 0, false);
}

bool unregisterThroughFacade(
    game_feature_ownership::RegistrationStore& registrations,
    MOBase::IPlugin* caller, const std::shared_ptr<MOBase::GameFeature>& feature)
{
  return game_feature_ownership::acceptsCallerSuppliedFeature(feature) &&
         registrations.unregisterFeature(caller, feature);
}

TEST(GameFeatureOwnershipTest, ForeignProxyCannotBeUnregisteredThenRegistered)
{
  PluginToken originToken;
  PluginToken callerToken;
  auto* const origin = pluginPointer(originToken);
  auto* const caller = pluginPointer(callerToken);

  game_feature_ownership::RegistrationStore registrations;
  const auto underlying = std::make_shared<TestGamePlugins>();
  ASSERT_TRUE(registrations.registerFeature(origin, {}, underlying, 0, false));

  const auto facade = std::make_shared<ForeignGamePluginsProxy>(underlying);
  ASSERT_FALSE(game_feature_ownership::acceptsCallerSuppliedFeature(facade));

  // This is the original laundering sequence. Neither facade operation may
  // unwrap the capability, so the origin registration remains intact.
  EXPECT_FALSE(unregisterThroughFacade(registrations, caller, facade));
  EXPECT_FALSE(registerThroughFacade(registrations, caller, facade));
  EXPECT_TRUE(registrations.ownedBy(typeid(MOBase::GamePlugins), underlying,
                                    origin));
  EXPECT_FALSE(registrations.ownedBy(typeid(MOBase::GamePlugins), underlying,
                                     caller));

  // Owner-scoped inventory removal is a second line of defense if a foreign
  // raw pointer is obtained through a non-facade route.
  EXPECT_FALSE(registrations.unregisterFeature(caller, underlying));
  EXPECT_FALSE(registrations.registerFeature(caller, {}, underlying, 0, false));
  EXPECT_TRUE(registrations.ownedBy(typeid(MOBase::GamePlugins), underlying,
                                    origin));
}

TEST(GameFeatureOwnershipTest, OwnerKeepsExactPointerAndCanUnregister)
{
  PluginToken ownerToken;
  auto* const owner = pluginPointer(ownerToken);

  game_feature_ownership::RegistrationStore registrations;
  const auto first = std::make_shared<TestGamePlugins>();
  ASSERT_TRUE(registerThroughFacade(registrations, owner, first));
  ASSERT_TRUE(
      registrations.ownedBy(typeid(MOBase::GamePlugins), first, owner));

  const auto& entries =
      registrations.inventory().at(std::type_index(typeid(MOBase::GamePlugins)));
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries.front().feature(), first);

  EXPECT_TRUE(unregisterThroughFacade(registrations, owner, first));
  EXPECT_FALSE(unregisterThroughFacade(registrations, owner, first));

  const auto second = std::make_shared<TestGamePlugins>();
  ASSERT_TRUE(registerThroughFacade(registrations, owner, first));
  ASSERT_TRUE(registerThroughFacade(registrations, owner, second));
  EXPECT_EQ(registrations.unregisterFeatures(owner, typeid(MOBase::GamePlugins)),
            2);
  EXPECT_FALSE(
      registrations.ownedBy(typeid(MOBase::GamePlugins), first, owner));
  EXPECT_FALSE(
      registrations.ownedBy(typeid(MOBase::GamePlugins), second, owner));
}

}  // namespace

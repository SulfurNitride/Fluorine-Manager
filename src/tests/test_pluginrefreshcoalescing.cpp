#include "pluginrefreshcoalescing.h"

#include <gtest/gtest.h>

TEST(PluginRefreshCoalescingTest, RetriesAfterLaterForcedFailure)
{
  PluginRefreshCoalescing state;
  const auto before = state.snapshot();

  state.complete(state.begin(true), true);
  state.complete(state.begin(true), false);

  EXPECT_FALSE(state.canSkipFallbackSince(before));
}

TEST(PluginRefreshCoalescingTest, CoalescesAfterLatestForcedSuccess)
{
  PluginRefreshCoalescing state;
  const auto before = state.snapshot();

  state.complete(state.begin(true), true);
  state.complete(state.begin(true), true);

  EXPECT_TRUE(state.canSkipFallbackSince(before));
}

TEST(PluginRefreshCoalescingTest, RetriesAfterLaterNonForcedFailure)
{
  PluginRefreshCoalescing state;
  const auto before = state.snapshot();

  state.complete(state.begin(true), true);
  state.complete(state.begin(false), false);

  EXPECT_FALSE(state.canSkipFallbackSince(before));
}

TEST(PluginRefreshCoalescingTest, DoesNotCoalesceWithoutACallbackRefresh)
{
  PluginRefreshCoalescing state;

  EXPECT_FALSE(state.canSkipFallbackSince(state.snapshot()));
}

TEST(PluginRefreshCoalescingTest, OlderOuterSuccessCannotMaskInnerFailure)
{
  PluginRefreshCoalescing state;
  const auto before = state.snapshot();
  const auto outer = state.begin(true);
  const auto inner = state.begin(true);

  state.complete(inner, false);
  state.complete(outer, true);

  EXPECT_FALSE(state.canSkipFallbackSince(before));
}

TEST(PluginRefreshCoalescingTest, OlderOuterSuccessCannotMaskInnerNonForcedRefresh)
{
  PluginRefreshCoalescing state;
  const auto before = state.snapshot();
  const auto outer = state.begin(true);
  const auto inner = state.begin(false);

  state.complete(inner, true);
  state.complete(outer, true);

  EXPECT_FALSE(state.canSkipFallbackSince(before));
}

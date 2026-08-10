#ifndef PROXYUTILS_H
#define PROXYUTILS_H

#include <type_traits>

#include "organizerproxy.h"

namespace MOShared
{

template <class Fn, class T = int>
auto callIfPluginActive(OrganizerProxy* proxy, Fn&& callback, T defaultReturn = T{})
{
  const auto gate = proxy->mutationGate();
  return [gate, fn = std::forward<Fn>(callback), proxy,
          defaultReturn](auto&&... args) {
    using Result =
        std::invoke_result_t<std::decay_t<Fn>, decltype(args)...>;

    if constexpr (std::is_same_v<Result, void>) {
      gate->runIfAllowed([&] {
        if (proxy->isPluginEnabled(proxy->plugin())) {
          fn(std::forward<decltype(args)>(args)...);
        }
      });
    } else {
      Result result = defaultReturn;
      gate->runIfAllowed([&] {
        if (proxy->isPluginEnabled(proxy->plugin())) {
          result = fn(std::forward<decltype(args)>(args)...);
        }
      });
      return result;
    }
  };
}

// We need to connect to the organizer.
template <class Signal, class T = int>
auto callSignalIfPluginActive(OrganizerProxy* proxy, const Signal& signal,
                              T defaultReturn = T{})
{
  return callIfPluginActive(
      proxy,
      [&signal](auto&&... args) {
        return signal(std::forward<decltype(args)>(args)...);
      },
      defaultReturn);
}

template <class Signal, class T = int>
auto callSignalAlways(OrganizerProxy* proxy, const Signal& signal,
                      T defaultReturn = T{})
{
  const auto gate     = proxy->mutationGate();
  return [gate, signal = &signal,
          defaultReturn](auto&&... args) {
    using Result = std::invoke_result_t<Signal, decltype(args)...>;
    if constexpr (std::is_same_v<Result, void>) {
      gate->runIfAllowed(
          [&] { (*signal)(std::forward<decltype(args)>(args)...); });
    } else {
      Result result = defaultReturn;
      gate->runIfAllowed(
          [&] { result = (*signal)(std::forward<decltype(args)>(args)...); });
      return result;
    }
  };
}

}  // namespace MOShared

#endif

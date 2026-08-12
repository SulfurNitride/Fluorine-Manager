#ifndef PLUGINREGISTRATIONTRANSACTION_H
#define PLUGINREGISTRATIONTRANSACTION_H

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace PluginRegistration
{

/**
 * Small host-state transaction for plugin registration.
 *
 * Each stage records its inverse before applying the mutation. Destruction
 * rolls completed or partially completed stages back in reverse order unless
 * commit() was called. Initialization is memoized so a logical candidate is
 * never invoked more than once even if interface selection changes later.
 */
class Transaction
{
public:
  Transaction() = default;
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  ~Transaction() noexcept
  {
    if (m_Committed) {
      return;
    }
    for (auto rollback = m_Rollbacks.rbegin();
         rollback != m_Rollbacks.rend(); ++rollback) {
      try {
        (*rollback)();
      } catch (...) {
      }
    }
  }

  template <typename Apply, typename Rollback>
  void stage(Apply&& apply, Rollback&& rollback)
  {
    m_Rollbacks.emplace_back(std::forward<Rollback>(rollback));
    std::invoke(std::forward<Apply>(apply));
  }

  template <typename Initialize>
  bool initializeOnce(Initialize&& initialize)
  {
    if (!m_InitializationResult) {
      m_InitializationResult =
          std::invoke(std::forward<Initialize>(initialize));
    }
    return *m_InitializationResult;
  }

  void commit() noexcept
  {
    m_Committed = true;
    m_Rollbacks.clear();
  }

private:
  std::vector<std::function<void()>> m_Rollbacks;
  std::optional<bool> m_InitializationResult;
  bool m_Committed{false};
};

}  // namespace PluginRegistration

#endif  // PLUGINREGISTRATIONTRANSACTION_H

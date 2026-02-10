#ifndef MO_UIBASE_MEMOIZEDLOCK_INCLUDED
#define MO_UIBASE_MEMOIZEDLOCK_INCLUDED

// Do not put this in utility.h otherwise the C# projects will
// fail to compile since apparently <mutex> is not available in
// C++/CLI projects.

#include <functional>
#include <mutex>

namespace MOBase
{

/**
 * Class that can be used to perform thread-safe memoization.
 *
 * Each instance hold a flag indicating if the current value is up-to-date
 * or not. This flag can be reset using `invalidate()`. When the value is queried,
 * the flag is checked, and if it is not up-to-date, the given callback is used
 * to compute the value.
 *
 * The computation and update of the value is locked to avoid concurrent modifications.
 *
 * @tparam T Type of value ot memoized.
 * @tparam Fn Type of the callback.
 */
template <class T, class Fn = std::function<T()>>
class MemoizedLocked
{
public:
  template <class Callable>
  MemoizedLocked(Callable&& callable, T value = {})
      : m_Fn{std::forward<Callable>(callable)}, m_Value{std::move(value)}
  {}

  template <class... Args>
  T& value(Args&&... args) const
  {
    if (m_NeedUpdating) {
      std::scoped_lock lock(m_Mutex);
      if (m_NeedUpdating) {
        m_Value        = std::invoke(m_Fn, std::forward<Args>(args)...);
        m_NeedUpdating = false;
      }
    }
    return m_Value;
  }

  void invalidate() { m_NeedUpdating = true; }

private:
  mutable std::mutex m_Mutex;
  mutable std::atomic<bool> m_NeedUpdating{true};

  Fn m_Fn;
  mutable T m_Value;
};

}  // namespace MOBase

#endif

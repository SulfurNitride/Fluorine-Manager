#ifndef AFTERRUNREFRESHQUEUE_H
#define AFTERRUNREFRESHQUEUE_H

#include <QString>

#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

// Product-independent state for assigning post-run refresh requests to exact
// directory generations. OrganizerCore owns execution/thread affinity; this
// class only prevents a request from being completed by another generation.
class AfterRunRefreshQueue
{
public:
  struct Request
  {
    QString profileName;
    std::function<void()> complete;
  };

  void enqueue(Request request) { m_Pending.push_back(std::move(request)); }

  bool hasPending() const { return !m_Pending.empty(); }

  std::vector<Request> takePending()
  {
    return std::exchange(m_Pending, {});
  }

  void assign(std::uint64_t generation,
              std::vector<std::function<void()>> completions)
  {
    if (completions.empty()) {
      return;
    }

    auto& assigned = m_Assigned[generation];
    assigned.insert(assigned.end(),
                    std::make_move_iterator(completions.begin()),
                    std::make_move_iterator(completions.end()));
  }

  std::vector<std::function<void()>> takeAssigned(std::uint64_t generation)
  {
    const auto stored = m_Assigned.find(generation);
    if (stored == m_Assigned.end()) {
      return {};
    }

    auto completions = std::move(stored->second);
    m_Assigned.erase(stored);
    return completions;
  }

  // Terminal fail-stop will not schedule another directory generation. Retire
  // every completion, including requests that were waiting to be assigned, so
  // WaitForRefresh callers can release their mutation/launch ownership.
  std::vector<std::function<void()>> takeAllCompletionsForFailStop()
  {
    std::vector<std::function<void()>> completions;
    for (auto& entry : m_Assigned) {
      auto& assigned = entry.second;
      completions.insert(completions.end(),
                         std::make_move_iterator(assigned.begin()),
                         std::make_move_iterator(assigned.end()));
    }
    m_Assigned.clear();

    for (auto& request : m_Pending) {
      if (request.complete) {
        completions.push_back(std::move(request.complete));
      }
    }
    m_Pending.clear();
    return completions;
  }

private:
  std::vector<Request> m_Pending;
  std::map<std::uint64_t, std::vector<std::function<void()>>> m_Assigned;
};

#endif  // AFTERRUNREFRESHQUEUE_H

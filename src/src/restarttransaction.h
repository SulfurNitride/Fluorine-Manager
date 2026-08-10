#ifndef MODORGANIZER_RESTARTTRANSACTION_INCLUDED
#define MODORGANIZER_RESTARTTRANSACTION_INCLUDED

#include <cstddef>
#include <utility>

#include "shared/exitstate.h"

namespace restart_transaction
{

enum class RollbackResult
{
  Complete,
  PersistenceFailed,
  CacheFailed,
  EventReversalFailed,
  VerificationFailed
};

inline bool requiresFailStop(RollbackResult result)
{
  return result != RollbackResult::Complete;
}

template <class Authorize, class Commit>
ExitRequestResult authorizeThenCommit(bool restartRequired, Authorize&& authorize,
                                      Commit&& commit)
{
  if (restartRequired) {
    const auto result = std::forward<Authorize>(authorize)();
    if (result != ExitRequestResult::Authorized) {
      return result;
    }
  }

  std::forward<Commit>(commit)();
  return ExitRequestResult::Authorized;
}

// Refused Settings restarts have both persistent and live effects to undo.
// Keep the ordering explicit: restored caches must never be based on rejected
// persistence, and the result is not complete until the restored file has
// been verified.
template <class RestorePersistence, class RestoreCache, class ReverseEvents,
          class Verify>
RollbackResult restoreAfterRefusal(RestorePersistence&& restorePersistence,
                                   RestoreCache&& restoreCache,
                                   ReverseEvents&& reverseEvents,
                                   Verify&& verify)
{
  if (!std::forward<RestorePersistence>(restorePersistence)()) {
    return RollbackResult::PersistenceFailed;
  }
  if (!std::forward<RestoreCache>(restoreCache)()) {
    return RollbackResult::CacheFailed;
  }
  if (!std::forward<ReverseEvents>(reverseEvents)()) {
    return RollbackResult::EventReversalFailed;
  }
  if (!std::forward<Verify>(verify)()) {
    return RollbackResult::VerificationFailed;
  }
  return RollbackResult::Complete;
}

template <class States, class Changed, class Notify>
bool notifyChangedStatesOnce(const States& states, const Changed& changed,
                             Notify&& notify)
{
  if (states.size() != changed.size()) {
    return false;
  }
  for (std::size_t i = 0; i < states.size(); ++i) {
    if (changed[i]) {
      std::forward<Notify>(notify)(states[i]);
    }
  }
  return true;
}

}  // namespace restart_transaction

#endif  // MODORGANIZER_RESTARTTRANSACTION_INCLUDED

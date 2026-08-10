#ifndef MODORGANIZER_EXITSTATE_INCLUDED
#define MODORGANIZER_EXITSTATE_INCLUDED

enum class ExitRequestResult
{
  // canExit accepted the request; the caller may now tell qApp to exit.
  Authorized,
  // canExit completed but rejected the request.
  Refused,
  // Another request is inside canExit. This is never authorization: that
  // outer request may still be refused after processing nested events.
  InProgress
};

// Exit authorization survives qApp->exit() long enough for the main window's
// final close event. Fluorine can restart in-process, so that authorization
// must be reset before constructing the next core.
class ExitState
{
public:
  // Run one exit authorization attempt. Keeping the reentrancy state and the
  // result in this helper makes nested event-loop calls unambiguous.
  template <class Authorize>
  ExitRequestResult requestAuthorization(Authorize&& authorize)
  {
    if (m_exiting) {
      return ExitRequestResult::InProgress;
    }

    m_exiting = true;
    struct AttemptGuard
    {
      explicit AttemptGuard(ExitState& state) : state(state) {}
      ~AttemptGuard() { state.m_exiting = false; }

      ExitState& state;
    } guard(*this);

    if (!authorize()) {
      return ExitRequestResult::Refused;
    }

    m_canClose = true;
    return ExitRequestResult::Authorized;
  }

  bool exiting() const { return m_exiting; }
  bool canClose() const { return m_canClose; }

  void resetForRestart()
  {
    m_exiting  = false;
    m_canClose = false;
  }

private:
  bool m_exiting{false};
  bool m_canClose{false};
};

#endif  // MODORGANIZER_EXITSTATE_INCLUDED

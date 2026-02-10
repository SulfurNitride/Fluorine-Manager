#ifndef UIBASE_MOASSERT_INCLUDED
#define UIBASE_MOASSERT_INCLUDED

#include "log.h"
#include <csignal>

namespace MOBase
{

template <class T>
inline void MOAssert(T&& t, const char* exp, const char* file, int line,
                     const char* func)
{
  if (!t) {
    log::error("assertion failed: {}:{} {}: '{}'", file, line, func, exp);

#ifdef _WIN32
    if (IsDebuggerPresent()) {
      DebugBreak();
    }
#else
    // On Linux, raise SIGTRAP if a debugger might be attached
    raise(SIGTRAP);
#endif
  }
}

}  // namespace MOBase

#ifdef _MSC_VER
#define MO_ASSERT(v) MOBase::MOAssert(v, #v, __FILE__, __LINE__, __FUNCSIG__)
#else
#define MO_ASSERT(v) MOBase::MOAssert(v, #v, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

#endif  // UIBASE_MOASSERT_INCLUDED

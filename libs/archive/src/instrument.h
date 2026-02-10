/*
Mod Organizer archive handling

Copyright (C) 2020 MO2 Team. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef ARCHIVE_INSTRUMENT_H
#define ARCHIVE_INSTRUMENT_H

#include <chrono>
#include <format>

namespace ArchiveTimers
{

/**
 * If INSTRUMENT_ARCHIVE is not defined, we define a Timer() class that is empty and
 * does nothing, and thus should be optimized out by the compiler.
 */
#ifdef INSTRUMENT_ARCHIVE

/**
 * Small class that can be used to instrument portion of code using a guard.
 *
 */
class Timer
{
public:
  using clock_t = std::chrono::system_clock;

  struct TimerGuard
  {

    TimerGuard(TimerGuard const&)            = delete;
    TimerGuard(TimerGuard&&)                 = delete;
    TimerGuard& operator=(TimerGuard const&) = delete;
    TimerGuard& operator=(TimerGuard&&)      = delete;

    ~TimerGuard()
    {
      m_Timer.ncalls++;
      m_Timer.time += clock_t::now() - m_Start;
    }

  private:
    TimerGuard(Timer& timer) : m_Timer{timer}, m_Start{clock_t::now()} {}

    Timer& m_Timer;
    clock_t::time_point m_Start;

    friend class Timer;
  };

  /**
   *
   */
  Timer() = default;

  /**
   * @brief Instrument a portion of code.
   *
   * Instrumenting is done by calling `instrument()` and storing the result in a local
   * variable. The scope of the local guard variable is the scope of the
   * instrumentation.
   *
   * @return a guard to instrument the code.
   */
  TimerGuard instrument() { return {*this}; }

  std::wstring toString(std::wstring const& name) const
  {
    auto ms = [](auto&& t) {
      return std::chrono::duration<double, std::milli>(t);
    };
    return std::format(
        L"Instrument '{}': {} calls, total of {}ms, {:.3f}ms per call on average.",
        name, ncalls, ms(time).count(), ms(time).count() / ncalls);
  }

private:
  std::size_t ncalls{0};
  clock_t::duration time{0};
};

#else

struct Timer
{
  // This is the only method needed:
  Timer& instrument() { return *this; }
};

#endif

}  // namespace ArchiveTimers

#endif

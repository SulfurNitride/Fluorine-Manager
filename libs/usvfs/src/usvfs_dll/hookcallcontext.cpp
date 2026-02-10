/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#include "hookcallcontext.h"
#include "hookcontext.h"
#include <logging.h>

namespace usvfs
{

class HookStack
{
public:
  static HookStack& instance()
  {
    PreserveGetLastError boostChangesGetLastError;
    if (s_Instance.get() == nullptr) {
      s_Instance.reset(new HookStack());
    }
    return *s_Instance.get();
  }

  bool setGroup(MutExHookGroup group)
  {
    if (m_ActiveGroups.test(static_cast<size_t>(group)) ||
        m_ActiveGroups.test(static_cast<size_t>(MutExHookGroup::ALL_GROUPS))) {
      return false;
    } else {
      m_ActiveGroups.set(static_cast<size_t>(group), true);
      return true;
    }
  }

  void unsetGroup(MutExHookGroup group)
  {
    m_ActiveGroups.set(static_cast<size_t>(group), false);
  }

private:
  HookStack() {}

private:
  static boost::thread_specific_ptr<HookStack> s_Instance;
  std::bitset<static_cast<size_t>(MutExHookGroup::LAST)> m_ActiveGroups;
};

boost::thread_specific_ptr<HookStack> HookStack::s_Instance;

HookCallContext::HookCallContext() : m_Active(true), m_Group(MutExHookGroup::NO_GROUP)
{
  updateLastError();
}

HookCallContext::HookCallContext(MutExHookGroup group)
    : m_Active(HookStack::instance().setGroup(group)), m_Group(group)
{
  updateLastError();
}

HookCallContext::~HookCallContext()
{
  if (m_Active && (m_Group != MutExHookGroup::NO_GROUP)) {
    HookStack::instance().unsetGroup(m_Group);
  }
  SetLastError(m_LastError);
}

void HookCallContext::restoreLastError()
{
  SetLastError(m_LastError);
}

void HookCallContext::updateLastError(DWORD lastError)
{
  m_LastError = lastError;
}

bool HookCallContext::active() const
{
  return m_Active;
}

FunctionGroupLock::FunctionGroupLock(MutExHookGroup group) : m_Group(group)
{
  m_Active = HookStack::instance().setGroup(m_Group);
}

FunctionGroupLock::~FunctionGroupLock()
{
  if (m_Active) {
    HookStack::instance().unsetGroup(m_Group);
  }
}

}  // namespace usvfs

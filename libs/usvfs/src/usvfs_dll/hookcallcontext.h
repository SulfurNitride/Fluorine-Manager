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
#pragma once

namespace usvfs
{

/**
 * @brief groups of hooks which may be used to implement each other, so only the first
 * call should be to one should be manipulated
 */
enum class MutExHookGroup : int
{
  ALL_GROUPS = 0,  // An ALL_GROUPS-hook prevents all other hooks from becoming active
                   // BUT hooks from other groups don't prevent the ALL_GROUPS-hook from
                   // becoming activated
  OPEN_FILE         = 1,
  CREATE_PROCESS    = 2,
  FILE_ATTRIBUTES   = 3,
  FIND_FILES        = 4,
  LOAD_LIBRARY      = 5,
  FULL_PATHNAME     = 6,
  SHELL_FILEOP      = 7,
  DELETE_FILE       = 8,
  GET_FILE_VERSION  = 9,
  GET_MODULE_HANDLE = 10,
  SEARCH_FILES      = 11,

  NO_GROUP = 12,
  LAST     = NO_GROUP,
};

class HookCallContext
{

public:
  HookCallContext();
  HookCallContext(MutExHookGroup group);
  ~HookCallContext();

  HookCallContext(const HookCallContext& reference)            = delete;
  HookCallContext& operator=(const HookCallContext& reference) = delete;

  void restoreLastError();

  void updateLastError(DWORD lastError = GetLastError());

  DWORD lastError() const { return m_LastError; }

  bool active() const;

private:
  DWORD m_LastError;
  bool m_Active;
  MutExHookGroup m_Group;
};

class FunctionGroupLock
{
public:
  FunctionGroupLock(MutExHookGroup group);
  ~FunctionGroupLock();

private:
  MutExHookGroup m_Group;
  bool m_Active;
};

}  // namespace usvfs

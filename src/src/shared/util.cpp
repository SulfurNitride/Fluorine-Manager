/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "util.h"
#include "exitstate.h"
#include "../env.h"
#include "../mainwindow.h"
#include "windows_error.h"

#include <fluorine_build_info.h>
#include <uibase/log.h>

#include <pthread.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>

using namespace MOBase;

namespace MOShared
{

bool FileExists(const std::string& filename)
{
  return std::filesystem::exists(filename);
}

bool FileExists(const std::wstring& filename)
{
  return std::filesystem::exists(std::filesystem::path(filename));
}

bool FileExists(const std::wstring& searchPath, const std::wstring& filename)
{
  std::wstringstream stream;
  stream << searchPath << "\\" << filename;
  return FileExists(stream.str());
}

std::string ToString(const std::wstring& source, bool utf8)
{
  Q_UNUSED(utf8);
  return QString::fromStdWString(source).toStdString();
}

std::wstring ToWString(const std::string& source, bool utf8)
{
  Q_UNUSED(utf8);
  return QString::fromStdString(source).toStdWString();
}

static std::locale makeUserLocale()
{
  // std::locale("") reads LANG/LC_* env vars. If the user's system lacks the
  // requested locale (e.g. en_US.UTF-8 not generated), the constructor throws
  // runtime_error. Fall back to "C" so startup doesn't abort.
  try {
    return std::locale("");
  } catch (const std::runtime_error&) {
    return std::locale::classic();
  }
}

static std::locale loc = makeUserLocale();

std::string& ToLowerInPlace(std::string& text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](char c) {
    return std::tolower(static_cast<unsigned char>(c));
  });
  return text;
}

std::string ToLowerCopy(const std::string& text)
{
  std::string result(text);
  return ToLowerInPlace(result);
}

std::wstring& ToLowerInPlace(std::wstring& text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
    return std::towlower(c);
  });
  return text;
}

std::wstring ToLowerCopy(const std::wstring& text)
{
  std::wstring result(text);
  return ToLowerInPlace(result);
}

std::wstring ToLowerCopy(std::wstring_view text)
{
  std::wstring result(text.begin(), text.end());
  ToLowerInPlace(result);
  return result;
}

bool CaseInsenstiveComparePred(wchar_t lhs, wchar_t rhs)
{
  return std::tolower(lhs, loc) == std::tolower(rhs, loc);
}

bool CaseInsensitiveEqual(const std::wstring& lhs, const std::wstring& rhs)
{
  return (lhs.length() == rhs.length()) &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                    [](wchar_t lhs, wchar_t rhs) -> bool {
                      return std::tolower(lhs, loc) == std::tolower(rhs, loc);
                    });
}

Version createVersionInfo()
{
  // Fluorine Manager version is the user-facing one. The numeric components
  // are injected by CMake from top-level FLUORINE_VERSION_* variables.
#if FLUORINE_IS_NIGHTLY_BUILD
  // Nightly builds tag themselves as Development pre-releases so the update
  // checker can distinguish them from stable tags when comparing versions.
  return Version(FLUORINE_VERSION_MAJOR, FLUORINE_VERSION_MINOR,
                 FLUORINE_VERSION_PATCH, 0, {Version::Development});
#else
  return {FLUORINE_VERSION_MAJOR, FLUORINE_VERSION_MINOR,
                 FLUORINE_VERSION_PATCH, 0};
#endif
}

void SetThisThreadName(const QString& s)
{
  // pthread_setname_np is limited to 16 chars including the null terminator.
  std::string name = s.toStdString();
  if (name.size() > 15) {
    name.resize(15);
  }
  pthread_setname_np(pthread_self(), name.c_str());
}

char shortcutChar(const QAction* a)
{
  const auto text = a->text();

  for (int i = 0; i < text.size(); ++i) {
    const auto c = text[i];
    if (c == '&') {
      if (i >= (text.size() - 1)) {
        log::error("ampersand at the end");
        return 0;
      }

      return text[i + 1].toLatin1();
    }
  }

  log::error("action {} has no shortcut", text);
  return 0;
}

void checkDuplicateShortcuts(const QMenu& m)
{
  const auto actions = m.actions();

  for (int i = 0; i < actions.size(); ++i) {
    const auto* action1 = actions[i];
    if (action1->isSeparator()) {
      continue;
    }

    const char shortcut1 = shortcutChar(action1);
    if (shortcut1 == 0) {
      continue;
    }

    for (int j = i + 1; j < actions.size(); ++j) {
      const auto* action2 = actions[j];
      if (action2->isSeparator()) {
        continue;
      }

      const char shortcut2 = shortcutChar(action2);

      if (shortcut1 == shortcut2) {
        log::error("duplicate shortcut {} for {} and {}", shortcut1, action1->text(),
                   action2->text());

        break;
      }
    }
  }
}

}  // namespace MOShared

static ExitState g_exitState;

MainWindow* findMainWindow()
{
  for (auto* tl : qApp->topLevelWidgets()) {
    if (auto* mw = dynamic_cast<MainWindow*>(tl)) {
      return mw;
    }
  }

  return nullptr;
}

ExitRequestResult ExitModOrganizer(ExitFlags e, bool silentActiveLaunch)
{
  const auto result = g_exitState.requestAuthorization([&] {
    if (auto* mw = findMainWindow()) {
      if (!mw->canExit(e.testFlag(Exit::Force), silentActiveLaunch)) {
        return false;
      }
    }
    return true;
  });

  if (result == ExitRequestResult::Authorized) {
    const int code = (e.testFlag(Exit::Restart) ? RestartExitCode : 0);
    qApp->exit(code);
  }

  return result;
}

bool ModOrganizerCanCloseNow()
{
  return g_exitState.canClose();
}

bool ModOrganizerExiting()
{
  return g_exitState.exiting();
}

void ResetExitFlag()
{
  g_exitState.resetForRestart();
}

bool isNxmLink(const QString& link)
{
  return link.startsWith("nxm://", Qt::CaseInsensitive) ||
         link.startsWith("modl://", Qt::CaseInsensitive);
}

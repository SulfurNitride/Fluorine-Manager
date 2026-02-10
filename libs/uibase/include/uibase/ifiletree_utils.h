/*
Mod Organizer shared UI functionality

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

#ifndef UIBASE_IFILETREE_UTILS_H
#define UIBASE_IFILETREE_UTILS_H

#include <generator>

#include <QString>

#include "dllimport.h"
#include "exceptions.h"
#include "ifiletree.h"

namespace MOBase
{

/**
 * @brief Exception thrown when an invalid glob pattern is specified.
 */
struct QDLLEXPORT InvalidGlobPatternException : public Exception
{
  using Exception::Exception;
};

enum class GlobPatternType
{
  /**
   * @brief Glob mode, similar to python pathlib.Path.glob function
   */
  GLOB,

  /**
   * @brief Regex mode, each part of the pattern (between / or \) is considered a
   * regex, except for ** which is still considered as glob.
   */
  REGEX
};

/**
 * @brief Walk this tree, returning entries.
 *
 * During the walk, parent tree are guaranteed to be visited before their childrens.
 * The current tree is not included in the return generator.
 *
 * @return a generator over the entries.
 */
QDLLEXPORT std::generator<std::shared_ptr<const FileTreeEntry>>
walk(std::shared_ptr<const IFileTree> fileTree);

/**
 * @brief Glob entries matching the given pattern in this tree.
 *
 * @param pattern Glob pattern to match, using the same syntax as QRegularExpression.
 * @param patternType Type of the pattern.
 *
 * @return a generator over the entries matching the given pattern.
 */
QDLLEXPORT std::generator<std::shared_ptr<const FileTreeEntry>>
glob(std::shared_ptr<const IFileTree> fileTree, QString pattern,
     GlobPatternType patternType = GlobPatternType::GLOB);

}  // namespace MOBase

#endif

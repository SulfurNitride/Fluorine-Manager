/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

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

#ifndef DIAGNOSISREPORT_H
#define DIAGNOSISREPORT_H

#include <QString>

namespace MOBase
{

/**
 * @brief report for a single problem reported by a plugin
 */
struct ProblemReport
{
  QString key;  // a plugin-defined unique key for the issue. This is used to refer to
                // the problem
  enum
  {
    SEVERITY_REPORT,       // the issue should be reported but nothing more
    SEVERITY_BREAKPLUGIN,  // the issue breaks the plugin (the plugin has to disable
                           // itself)
    SEVERITY_BREAKGAME     // the issue will (likely) break the game. The user will be
                           // warned about this every time he tries to start
  } severity;
  bool guidedFix;            // if true, the plugin provides a guide to fixing the issue
  QString shortDescription;  // short description text for the overview
  QString longDescription;   //
};

}  // namespace MOBase

#endif  // DIAGNOSISREPORT_H

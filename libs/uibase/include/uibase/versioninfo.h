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

#ifndef MODVERSION_H
#define MODVERSION_H

#include "dllimport.h"
#include <QList>
#include <QString>

class QVersionNumber;

namespace MOBase
{

/**
 * @brief represents the version of a mod or plugin
 *
 * this will try to parse machine-readable information from a string.
 **/
class QDLLEXPORT VersionInfo
{

  friend QDLLEXPORT bool operator<(const VersionInfo& LHS, const VersionInfo& RHS);
  friend QDLLEXPORT bool operator>(const VersionInfo& LHS, const VersionInfo& RHS);
  friend QDLLEXPORT bool operator<=(const VersionInfo& LHS, const VersionInfo& RHS);
  friend QDLLEXPORT bool operator>=(const VersionInfo& LHS, const VersionInfo& RHS);
  friend QDLLEXPORT bool operator!=(const VersionInfo& LHS, const VersionInfo& RHS);
  friend QDLLEXPORT bool operator==(const VersionInfo& LHS, const VersionInfo& RHS);

public:
  enum ReleaseType
  {
    RELEASE_PREALPHA,
    RELEASE_ALPHA,
    RELEASE_BETA,
    RELEASE_CANDIDATE,
    RELEASE_FINAL
  };

  enum VersionScheme
  {
    SCHEME_DISCOVER,  // use regular scheme unless the string contains a hint that it's
                      // one of the others
    SCHEME_REGULAR,
    SCHEME_DECIMALMARK,  // for schemes that treat the version as a decimal number with
                         // the dot as the decimal mark
    SCHEME_NUMBERSANDLETTERS,  // for schemes that mix numbers and letters
                               // (1.0.1a, 1.0.1c, ...). otherwise this is the regular
                               // scheme
    SCHEME_DATE,               // contains a release date instead of a version number
    SCHEME_LITERAL             // use the version string as is, unmodified
  };

public:
  /**
   * @brief default constructor
   * constructs an invalid version
   **/
  VersionInfo();

  /**
   * @brief constructor
   * @param major major version
   * @param minor minor version
   * @param subminor subminor version
   * @param releaseType release type
   */
  VersionInfo(int major, int minor, int subminor,
              ReleaseType releaseType = RELEASE_FINAL);

  /**
   * @brief constructor
   * @param major major version
   * @param minor minor version
   * @param subminor subminor version
   * @param subsubminor subsubminor version
   * @param releaseType release type
   */
  VersionInfo(int major, int minor, int subminor, int subsubminor,
              ReleaseType releaseType = RELEASE_FINAL);

  /**
   * @brief constructor
   * @param versionString the string to construct from
   **/
  VersionInfo(const QString& versionString, VersionScheme scheme = SCHEME_DISCOVER);

  /**
   * @brief constructor
   * @param versionString the string to construct from
   * @param manualInput if true the versionString is treated as input from a user
   **/
  VersionInfo(const QString& versionString, VersionScheme scheme, bool manualInput);

  /**
   * @brief resets this structure to an invalid version
   */
  void clear();

  /**
   * @brief parse the version from the specified string
   *
   * @param versionString the string to parse
   **/
  void parse(const QString& versionString, VersionScheme scheme = SCHEME_DISCOVER,
             bool manualInput = false);

  /**
   * @return a canonicalized version string
   * @note due to support for different versioning schemes this somewhat lost it's
   *original intention. This is now supposed to return a version string that can be
   *parsed to re-create this VersionInfo without information loss.
   **/
  QString canonicalString() const;

  /**
   * @return a version string for display to the user. This may loose information as it
   * doesn't contain information about the versioning scheme
   *
   * @param forcedVersionSegments the number of version segments to display even if the
   * version is 0.  1 is major, 2 is major and minor, etc. The only implemented ranges
   * are (-inf,2] for major/minor, [3] for major/minor/subminor, and [4,inf) for
   * major/minor/subminor/subsubminor. This only versions with a regular scheme.
   */
  QString displayString(int forcedVersionSegments = 2) const;

  /**
   * @return true if this version is valid, false if it wasn't initialised or
   *         the version string was not parsable
   */
  bool isValid() const { return m_Valid; }

  /**
   * @return the versioning scheme in effect
   */
  VersionScheme scheme() const { return m_Scheme; }

  // returns this version number as a QVersionNumber
  //
  QVersionNumber asQVersionNumber() const;

private:
  /**
   * @brief determine the release type
   * @param versionString the version string to parse
   * @return the version string with the release type removed
   **/
  QString parseReleaseType(QString versionString);

private:
  VersionScheme m_Scheme;

  bool m_Valid;
  ReleaseType m_ReleaseType;
  int m_Major;
  int m_Minor;
  int m_SubMinor;
  int m_SubSubMinor;

  int m_DecimalPositions;

  QString m_Rest;
};

}  // namespace MOBase

#endif  // MODVERSION_H

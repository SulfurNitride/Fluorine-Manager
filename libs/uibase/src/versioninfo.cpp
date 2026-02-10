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

#include <uibase/versioninfo.h>
#include <QDate>
#include <QLocale>
#include <QRegularExpression>
#include <QVersionNumber>

namespace
{
const QRegularExpression VERSION_REGEX("^(\\d+)(\\.(\\d+))?(\\.(\\d+))?(\\.(\\d+))?");
}

namespace MOBase
{

VersionInfo::VersionInfo()
    : m_Scheme(SCHEME_REGULAR), m_Valid(false), m_ReleaseType(RELEASE_FINAL),
      m_Major(0), m_Minor(0), m_SubMinor(0), m_SubSubMinor(0), m_DecimalPositions(0),
      m_Rest()
{}

VersionInfo::VersionInfo(int major, int minor, int subminor, int subsubminor,
                         ReleaseType releaseType)
    : m_Scheme(SCHEME_REGULAR), m_Valid(true), m_ReleaseType(releaseType),
      m_Major(major), m_Minor(minor), m_SubMinor(subminor), m_SubSubMinor(subsubminor),
      m_DecimalPositions(0), m_Rest()
{}

VersionInfo::VersionInfo(int major, int minor, int subminor, ReleaseType releaseType)
    : m_Scheme(SCHEME_REGULAR), m_Valid(true), m_ReleaseType(releaseType),
      m_Major(major), m_Minor(minor), m_SubMinor(subminor), m_SubSubMinor(0),
      m_DecimalPositions(0), m_Rest()
{}

VersionInfo::VersionInfo(const QString& versionString, VersionScheme scheme)
    : m_Valid(true), m_ReleaseType(RELEASE_FINAL), m_Major(0), m_Minor(0),
      m_SubMinor(0), m_SubSubMinor(0), m_DecimalPositions(0), m_Rest()
{
  parse(versionString, scheme);
}

VersionInfo::VersionInfo(const QString& versionString,
                         VersionInfo::VersionScheme scheme, bool manualInput)
    : m_Valid(true), m_ReleaseType(RELEASE_FINAL), m_Major(0), m_Minor(0),
      m_SubMinor(0), m_SubSubMinor(0), m_DecimalPositions(0), m_Rest()
{
  parse(versionString, scheme, manualInput);
}

void VersionInfo::clear()
{
  m_Scheme      = SCHEME_REGULAR;
  m_Valid       = false;
  m_ReleaseType = RELEASE_FINAL;
  m_Major = m_Minor = m_SubMinor = m_SubSubMinor = m_DecimalPositions = 0;
  m_Rest.clear();
}

QString VersionInfo::canonicalString() const
{
  if (!isValid()) {
    return QString();
  }

  QString result;
  if (m_Scheme == SCHEME_REGULAR) {
    result = QString("%1.%2.%3.%4")
                 .arg(m_Major)
                 .arg(m_Minor)
                 .arg(m_SubMinor)
                 .arg(m_SubSubMinor);
  } else if (m_Scheme == SCHEME_DECIMALMARK) {
    result = QString("f%1.%2").arg(m_Major).arg(
        QString("%1").arg(m_Minor).rightJustified(m_DecimalPositions, '0'));
  } else if (m_Scheme == SCHEME_NUMBERSANDLETTERS) {
    result = QString("n%1.%2.%3.%4")
                 .arg(m_Major)
                 .arg(m_Minor)
                 .arg(m_SubMinor)
                 .arg(m_SubSubMinor);
  } else if (m_Scheme == SCHEME_DATE) {
    // year.month.day was stored in the version fields
    result = QString("d%1.%2.%3.%4")
                 .arg(m_Major)
                 .arg(m_Minor)
                 .arg(m_SubMinor)
                 .arg(m_SubSubMinor);
  }
  switch (m_ReleaseType) {
  case RELEASE_PREALPHA: {
    result.append(" pre-alpha");
  } break;
  case RELEASE_ALPHA: {
    result.append("a");
  } break;
  case RELEASE_BETA: {
    result.append("b");
  } break;
  case RELEASE_CANDIDATE: {
    result.append("rc");
  } break;
  case RELEASE_FINAL:  // fall-through
  default: {
    // nop
  } break;
  }

  if (!m_Rest.isEmpty()) {
    result.append(QString("%1").arg(m_Rest));
  }

  return result;
}

QString VersionInfo::displayString(int forcedVersionSegments) const
{
  if (!isValid()) {
    return QString();
  }

  QString result;
  if (m_Scheme == SCHEME_REGULAR) {
    if (forcedVersionSegments >= 4 || m_SubSubMinor != 0) {
      result = QString("%1.%2.%3.%4")
                   .arg(m_Major)
                   .arg(m_Minor)
                   .arg(m_SubMinor)
                   .arg(m_SubSubMinor);
    } else if (forcedVersionSegments == 3 || m_SubMinor != 0) {
      result = QString("%1.%2.%3").arg(m_Major).arg(m_Minor).arg(m_SubMinor);
    } else {
      result = QString("%1.%2").arg(m_Major).arg(m_Minor);
    }
  } else if (m_Scheme == SCHEME_DECIMALMARK) {
    result = QString("%1.%2").arg(m_Major).arg(
        QString("%1").arg(m_Minor).rightJustified(m_DecimalPositions, '0'));
  } else if (m_Scheme == SCHEME_NUMBERSANDLETTERS) {
    result = QString("%1.%2.%3.%4")
                 .arg(m_Major)
                 .arg(m_Minor)
                 .arg(m_SubMinor)
                 .arg(m_SubSubMinor);
  } else if (m_Scheme == SCHEME_DATE) {
    // year.month.day was stored in the version fields
    const auto year  = m_Major;
    const auto month = m_Minor;
    const auto day   = m_SubMinor;

    return QLocale::system().toString(QDate(year, month, day),
                                      QLocale::FormatType::ShortFormat);
  }
  switch (m_ReleaseType) {
  case RELEASE_PREALPHA: {
    result.append(" pre-alpha");
  } break;
  case RELEASE_ALPHA: {
    result.append("alpha");
  } break;
  case RELEASE_BETA: {
    result.append("beta");
  } break;
  case RELEASE_CANDIDATE: {
    result.append("rc");
  } break;
  case RELEASE_FINAL:  // fall-through
  default: {
    // nop
  } break;
  }

  if (!m_Rest.isEmpty()) {
    result.append(QString("%1").arg(m_Rest));
  }

  return result;
}

QVersionNumber VersionInfo::asQVersionNumber() const
{
  return QVersionNumber::fromString(displayString()).normalized();
}

QString VersionInfo::parseReleaseType(QString versionString)
{
  // release types are often followed by a number (i.e. "beta4"). This needs to be
  // extracted now, otherwise the outer parser will think it's the subminor version and
  // then 1.0.0rc1 would be interpreted as newer than 1.0.0

  static std::map<QString, ReleaseType> typeStrings = {{"prealpha", RELEASE_PREALPHA},
                                                       {"alpha", RELEASE_ALPHA},
                                                       {"beta", RELEASE_BETA},
                                                       {"rc", RELEASE_CANDIDATE}};

  m_ReleaseType = RELEASE_FINAL;

  auto typeIter = typeStrings.begin();
  int offset    = -1;

  for (; (typeIter != typeStrings.end()) && (offset == -1); ++typeIter) {
    offset = (int)versionString.indexOf(typeIter->first, Qt::CaseInsensitive);
    if (offset != -1) {
      m_ReleaseType = typeIter->second;
      break;
    }
  }

  int length = 0;

  if (typeIter != typeStrings.end()) {
    length = (int)typeIter->first.length();
  }

  if (m_Scheme == SCHEME_REGULAR) {
    // also interpret the a/b letters, but only if they follow immediately on the
    // version number, otherwise the margin for error is too big
    if ((offset == -1) && (versionString.length() > 0)) {
      if (versionString.at(0) == 'a') {
        m_ReleaseType = RELEASE_ALPHA;
        offset        = 0;
        length        = 1;
      } else if (versionString.at(0) == 'b') {
        m_ReleaseType = RELEASE_BETA;
        offset        = 0;
        length        = 1;
      }
    }
  }

  if (offset != -1) {
    versionString.remove(offset, length);
  }
  return versionString.trimmed();
}

void VersionInfo::parse(const QString& versionString, VersionScheme scheme,
                        bool manualInput)
{
  m_Valid       = false;
  m_Scheme      = scheme == SCHEME_LITERAL    ? SCHEME_REGULAR
                  : scheme != SCHEME_DISCOVER ? scheme
                                              : SCHEME_REGULAR;
  m_ReleaseType = RELEASE_FINAL;
  m_Major = m_Minor = m_SubMinor = m_SubSubMinor = 0;
  m_Rest.clear();
  if (versionString.length() == 0) {
    return;
  }

  if (QString::compare(versionString, "final", Qt::CaseInsensitive) == 0) {
    m_Major = 1;
    m_Valid = true;
    return;
  }

  QString temp = versionString;
  // first, determine the versioning scheme if there is a hint
  VersionScheme newScheme = m_Scheme;
  if (!manualInput) {
    if (temp.startsWith('f')) {
      newScheme = SCHEME_DECIMALMARK;
      temp.remove(0, 1);
    } else if (temp.startsWith('n')) {
      newScheme = SCHEME_NUMBERSANDLETTERS;
      temp.remove(0, 1);
    } else if (temp.startsWith('d')) {
      newScheme = SCHEME_DATE;
      temp.remove(0, 1);
    }
  }

  if (scheme == SCHEME_DISCOVER) {
    m_Scheme = newScheme;
  }

  if (temp.startsWith('v', Qt::CaseInsensitive)) {
    // v is often prepended to versions
    temp.remove(0, 1);
  }

  auto match = VERSION_REGEX.match(temp);
  if (match.hasMatch()) {
    m_Major             = match.captured(1).toInt();
    m_Minor             = match.captured(3).toInt();
    QString subMinor    = match.captured(5);
    QString subSubMinor = match.captured(7);
    if (!subMinor.isEmpty() && (m_Scheme == SCHEME_DECIMALMARK)) {
      // nooooope, if there are two dots it can't be a decimal mark
      m_Scheme = SCHEME_REGULAR;
    }
    if (m_Scheme != SCHEME_DECIMALMARK) {
      m_SubMinor    = subMinor.toInt();
      m_SubSubMinor = subSubMinor.toInt();
    }
    if (subMinor.isEmpty() && (match.captured(3).size() > 1) &&
        match.captured(3).startsWith('0')) {
      // this indicates a decimal scheme
      m_Scheme           = SCHEME_DECIMALMARK;
      m_DecimalPositions = (int)match.captured(3).size();
    }
    temp.remove(VERSION_REGEX);
  } else {
    m_Scheme = SCHEME_LITERAL;
  }

  if (m_Scheme == SCHEME_REGULAR) {
    temp = parseReleaseType(temp);
  }

  if ((m_Scheme == SCHEME_DATE) && (m_Major < 1900)) {
    m_Scheme = SCHEME_REGULAR;
  }
  m_Rest  = temp.trimmed();
  m_Valid = true;
}

QDLLEXPORT bool operator<(const VersionInfo& LHS, const VersionInfo& RHS)
{
  if (!LHS.isValid() && RHS.isValid())
    return true;
  if (!RHS.isValid() && LHS.isValid())
    return false;

  // date-releases are lower than regular versions
  if ((LHS.m_Scheme == VersionInfo::SCHEME_DATE) &&
      (RHS.m_Scheme != VersionInfo::SCHEME_DATE)) {
    return true;
  } else if ((LHS.m_Scheme != VersionInfo::SCHEME_DATE) &&
             (RHS.m_Scheme == VersionInfo::SCHEME_DATE)) {
    return false;
  } else if ((LHS.m_Scheme == VersionInfo::SCHEME_DECIMALMARK) ||
             (RHS.m_Scheme == VersionInfo::SCHEME_DECIMALMARK)) {
    // use decimal versioning if either version is a decimal. The parser interprets
    // versions as regular if in doubt so if the scheme is "decimal" it is definitively
    // a decimal version number whereas SCHEME_REGULAR means "probably regular"

    float leftVal = QString("%1.%2")
                        .arg(LHS.m_Major)
                        .arg(QString("%1")
                                 .arg(LHS.m_Minor)
                                 .rightJustified(LHS.m_DecimalPositions, '0'))
                        .toFloat();
    float rightVal = QString("%1.%2")
                         .arg(RHS.m_Major)
                         .arg(QString("%1")
                                  .arg(RHS.m_Minor)
                                  .rightJustified(RHS.m_DecimalPositions, '0'))
                         .toFloat();
    if (fabs(leftVal - rightVal) > 0.001f) {
      return leftVal < rightVal;
    }
  } else {
    // if in doubt, use the sane choice. regular and numbers+letters can be treated the
    // same way
    if (LHS.m_Major != RHS.m_Major)
      return LHS.m_Major < RHS.m_Major;
    if (LHS.m_Minor != RHS.m_Minor)
      return LHS.m_Minor < RHS.m_Minor;
    if (LHS.m_SubMinor != RHS.m_SubMinor)
      return LHS.m_SubMinor < RHS.m_SubMinor;
    if (LHS.m_SubSubMinor != RHS.m_SubSubMinor)
      return LHS.m_SubSubMinor < RHS.m_SubSubMinor;
  }

  // subminor, release-type and rest are treated the same for all versioning schemes,
  // but on parsing they may still differ, i.e. a b-suffix is only interpreted to mean
  // "beta" in the regular scheme
  if (LHS.m_ReleaseType != RHS.m_ReleaseType)
    return LHS.m_ReleaseType < RHS.m_ReleaseType;

  // if the rest contains only integers, compare them numerically
  bool LHS_ok, RHS_ok;
  int LHS_int, RHS_int;
  LHS_int = LHS.m_Rest.toInt(&LHS_ok);
  RHS_int = RHS.m_Rest.toInt(&RHS_ok);
  if (LHS_ok && RHS_ok) {
    return LHS_int < RHS_int;
  }

  // give up and compare lexically
  return LHS.m_Rest < RHS.m_Rest;
}

QDLLEXPORT bool operator>(const VersionInfo& LHS, const VersionInfo& RHS)
{
  return !(LHS <= RHS);
}

QDLLEXPORT bool operator<=(const VersionInfo& LHS, const VersionInfo& RHS)
{
  // TODO not exactly optimized...
  if (LHS < RHS) {
    return true;
  } else {
    return !(RHS < LHS);
  }
}

QDLLEXPORT bool operator>=(const VersionInfo& LHS, const VersionInfo& RHS)
{
  return RHS <= LHS;
}

QDLLEXPORT bool operator!=(const VersionInfo& LHS, const VersionInfo& RHS)
{
  return (LHS < RHS) || (RHS < LHS);
}

QDLLEXPORT bool operator==(const VersionInfo& LHS, const VersionInfo& RHS)
{
  return !(LHS < RHS) && !(RHS < LHS);
}

}  // namespace MOBase

#pragma once

#include <compare>
#include <string>
#include <variant>
#include <vector>

#include <QFlags>
#include <QString>

#include "dllimport.h"
#include "exceptions.h"

namespace MOBase
{

class InvalidVersionException : public Exception
{
public:
  using Exception::Exception;
};

// class representing a Version object
//
// valid versions are an "extension" of SemVer (see https://semver.org/) with the
// following tweaks:
// - version can have a sub-patch, i.e., x.y.z.p, which are normally not allowed by
//   SemVer
// - non-integer pre-release identifiers are limited to dev, alpha (a), beta (b) and rc,
//   and dev is lower than alpha (according to SemVer, the pre-release should be
//   ordered alphabetically)
// - the '-' between version and pre-release can be made optional, and also the '.'
//   between pre-releases segment
//
// the extension from SemVer are only meant to be used by MO2 and USVFS versioning,
// plugins and extensions should follow SemVer standard (and not use dev), this is
// mainly
// - for back-compatibility purposes, because USVFS versioning contains sub-patches and
//   there are old MO2 releases with sub-patch
// - because MO2 is not going to become MO3, so having an extra level make sense
//
// unlike VersionInfo, this class is immutable and only hold valid versions
//
class QDLLEXPORT Version
{
public:
  enum class ParseMode
  {
    // official semver parsing with pre-release limited to dev, alpha/a, beta/b and rc
    //
    SemVer,

    // MO2 parsing, e.g., 2.5.1rc1 - this either parse a string with no pre-release
    // information (e.g. 2.5.1) or with a single pre-release + a version (e.g., 2.5.1a1
    // or 2.5.2rc1)
    //
    // this mode can parse sub-patch (SemVer mode cannot)
    //
    MO2
  };

  enum class FormatMode
  {
    // show subpatch even if subpatch is 0
    //
    ForceSubPatch = 0b0001,

    // do not add separators between version and pre-release (-) or between pre-release
    // segments (.)
    //
    NoSeparator = 0b0010,

    // uses short form for alpha and beta (a/b instead of alpha/beta)
    //
    ShortAlphaBeta = 0b0100,

    // do not add metadata even if present
    //
    NoMetadata = 0b1000
  };
  Q_DECLARE_FLAGS(FormatModes, FormatMode);

  // condensed format, no separator, short alpha/beta and no metadata
  //
  static constexpr auto FormatCondensed = FormatModes{
      FormatMode::NoSeparator, FormatMode::ShortAlphaBeta, FormatMode::NoMetadata};

  enum class ReleaseType
  {
    Development,       // -dev
    Alpha,             // -alpha, -a
    Beta,              // -beta, -b
    ReleaseCandidate,  // -rc
  };
  using enum ReleaseType;

public:  // parsing
  // parse version from the given string, throw InvalidVersionException if the string
  // cannot be parsed
  //
  static Version parse(QString const& value, ParseMode mode = ParseMode::SemVer);

public:  // constructors
  Version(int major, int minor, int patch, QString metadata = {});
  Version(int major, int minor, int patch, int subpatch, QString metadata = {});

  Version(int major, int minor, int patch, ReleaseType type, QString metadata = {});
  Version(int major, int minor, int patch, int subpatch, ReleaseType type,
          QString metadata = {});

  Version(int major, int minor, int patch, ReleaseType type, int prerelease,
          QString metadata = {});
  Version(int major, int minor, int patch, int subpatch, ReleaseType type,
          int prerelease, QString metadata = {});

  Version(int major, int minor, int patch, int subpatch,
          std::vector<std::variant<int, ReleaseType>> prereleases,
          QString metadata = {});

public:  // special member functions
  Version(const Version&) = default;
  Version(Version&&)      = default;

  Version& operator=(const Version&) = default;
  Version& operator=(Version&&)      = default;

public:
  // check if this version corresponds to a pre-release version (dev, alpha, beta, etc.)
  //
  bool isPreRelease() const { return !m_PreReleases.empty(); }

  // retrieve major, minor, patch and sub-patch of this version
  //
  int major() const { return m_Major; }
  int minor() const { return m_Minor; }
  int patch() const { return m_Patch; }
  int subpatch() const { return m_SubPatch; }

  // retrieve pre-releases information for this version
  //
  const auto& preReleases() const { return m_PreReleases; }

  // retrieve build metadata, if any, otherwise return an empty string
  //
  const auto& buildMetadata() const { return m_BuildMetadata; }

  // convert this version to a string
  //
  QString string(const FormatModes& modes = {}) const;

private:
  // major.minor.patch
  int m_Major, m_Minor, m_Patch, m_SubPatch;

  // pre-release information
  std::vector<std::variant<int, ReleaseType>> m_PreReleases;

  // metadata
  QString m_BuildMetadata;
};

QDLLEXPORT std::strong_ordering operator<=>(const Version& lhs, const Version& rhs);

inline bool operator==(const Version& lhs, const Version& rhs)
{
  return (lhs <=> rhs) == 0;
}

Q_DECLARE_OPERATORS_FOR_FLAGS(Version::FormatModes);

}  // namespace MOBase

template <class CharT>
struct std::formatter<MOBase::Version, CharT> : std::formatter<QString, CharT>
{
  template <class FmtContext>
  FmtContext::iterator format(const MOBase::Version& v, FmtContext& ctx) const
  {
    return std::formatter<QString, CharT>::format(v.string(), ctx);
  }
};

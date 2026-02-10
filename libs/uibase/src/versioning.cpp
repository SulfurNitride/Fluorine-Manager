#include <uibase/versioning.h>

#include <QRegularExpression>
#include <format>
#include <unordered_map>

#include <uibase/formatters.h>

// official semver regex
static const QRegularExpression s_SemVerStrictRegEx{
    R"(^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)"};

// for MO2, to match stuff like 1.2.3rc1 or v1.2.3a1+XXX
static const QRegularExpression s_SemVerMO2RegEx{
    R"(^v?(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:\.(?P<subpatch>0|[1-9]\d*))?(?:(?P<type>dev|a|alpha|b|beta|rc)(?P<prerelease>0|[1-9](?:[.0-9])*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)"};

// match from value to release type
static const std::unordered_map<QString, MOBase::Version::ReleaseType>
    s_StringToRelease{{"dev", MOBase::Version::Development},
                      {"alpha", MOBase::Version::Alpha},
                      {"a", MOBase::Version::Alpha},
                      {"beta", MOBase::Version::Beta},
                      {"b", MOBase::Version::Beta},
                      {"rc", MOBase::Version::ReleaseCandidate}};

namespace MOBase
{

namespace
{

  Version parseVersionSemVer(QString const& value)
  {
    const auto match = s_SemVerStrictRegEx.match(value);

    if (!match.hasMatch()) {
      throw InvalidVersionException(
          QString::fromStdString(std::format("invalid version string: '{}'", value)));
    }

    const auto major = match.captured("major").toInt();
    const auto minor = match.captured("minor").toInt();
    const auto patch = match.captured("patch").toInt();

    std::vector<std::variant<int, Version::ReleaseType>> prereleases;
    for (auto& part : match.captured("prerelease")
                          .split(".", Qt::SplitBehaviorFlags::SkipEmptyParts)) {
      // try to extract an int
      bool ok             = true;
      const auto intValue = part.toInt(&ok);
      if (ok) {
        prereleases.push_back(intValue);
        continue;
      }

      // check if we have a valid prerelease type
      const auto it = s_StringToRelease.find(part.toLower());
      if (it == s_StringToRelease.end()) {
        throw InvalidVersionException(
            QString::fromStdString(std::format("invalid prerelease type: '{}'", part)));
      }

      prereleases.push_back(it->second);
    }

    const auto buildMetadata = match.captured("buildmetadata").trimmed();

    return Version(major, minor, patch, 0, prereleases, buildMetadata);
  }

  Version parseVersionMO2(QString const& value)
  {
    const auto match = s_SemVerMO2RegEx.match(value);

    if (!match.hasMatch()) {
      throw InvalidVersionException(
          QString::fromStdString(std::format("invalid version string: '{}'", value)));
    }

    const auto major = match.captured("major").toInt();
    const auto minor = match.captured("minor").toInt();
    const auto patch = match.captured("patch").toInt();

    const auto subpatch = match.captured("subpatch").toInt();

    // unlike semver, the regex will only match valid values
    std::vector<std::variant<int, Version::ReleaseType>> prereleases;
    if (match.hasCaptured("type")) {
      prereleases.push_back(s_StringToRelease.at(match.captured("type")));

      // for version with decimal point, e.g., 2.4.1rc1.1, we split the components into
      // pre-release components to get {rc, 1, 1} - this works fine since {rc, 1} < {rc,
      // 1, 1}
      //
      for (const auto& preVersion :
           match.captured("prerelease").split(".", Qt::SkipEmptyParts)) {
        prereleases.push_back(preVersion.toInt());
      }
    }

    const auto buildMetadata = match.captured("buildmetadata").trimmed();

    return Version(major, minor, patch, subpatch, prereleases, buildMetadata);
  }

}  // namespace

Version Version::parse(QString const& value, ParseMode mode)
{
  return mode == ParseMode::SemVer ? parseVersionSemVer(value) : parseVersionMO2(value);
}

// constructors

Version::Version(int major, int minor, int patch, QString metadata)
    : Version(major, minor, patch, 0, std::move(metadata))
{}
Version::Version(int major, int minor, int patch, int subpatch, QString metadata)
    : m_Major{major}, m_Minor{minor}, m_Patch{patch}, m_SubPatch{subpatch},
      m_PreReleases{}, m_BuildMetadata{std::move(metadata)}
{}

Version::Version(int major, int minor, int patch, ReleaseType type, QString metadata)
    : Version(major, minor, patch, 0, type, std::move(metadata))
{}
Version::Version(int major, int minor, int patch, int subpatch, ReleaseType type,
                 QString metadata)
    : m_Major{major}, m_Minor{minor}, m_Patch{patch}, m_SubPatch{subpatch},
      m_PreReleases{type}, m_BuildMetadata{std::move(metadata)}
{}

Version::Version(int major, int minor, int patch, ReleaseType type, int prerelease,
                 QString metadata)
    : Version(major, minor, patch, 0, type, prerelease, std::move(metadata))
{}
Version::Version(int major, int minor, int patch, int subpatch, ReleaseType type,
                 int prerelease, QString metadata)
    : Version(major, minor, patch, subpatch, {type, prerelease}, std::move(metadata))
{}

Version::Version(int major, int minor, int patch, int subpatch,
                 std::vector<std::variant<int, ReleaseType>> prereleases,
                 QString metadata)
    : m_Major{major}, m_Minor{minor}, m_Patch{patch}, m_SubPatch{subpatch},
      m_PreReleases{std::move(prereleases)}, m_BuildMetadata{std::move(metadata)}
{}

// string

QString Version::string(const FormatModes& modes) const
{
  const bool noSeparator    = modes.testFlag(FormatMode::NoSeparator);
  const bool shortAlphaBeta = modes.testFlag(FormatMode::ShortAlphaBeta);
  auto value                = std::format("{}.{}.{}", m_Major, m_Minor, m_Patch);

  if (m_SubPatch || modes.testFlag(FormatMode::ForceSubPatch)) {
    value += std::format(".{}", m_SubPatch);
  }

  if (!m_PreReleases.empty()) {
    if (!noSeparator) {
      value += "-";
    }
    for (std::size_t i = 0; i < m_PreReleases.size(); ++i) {
      value += std::visit(
          [shortAlphaBeta](auto const& pre) -> std::string {
            if constexpr (std::is_same_v<decltype(pre), ReleaseType const&>) {
              switch (pre) {
              case Development:
                return "dev";
              case Alpha:
                return shortAlphaBeta ? "a" : "alpha";
              case Beta:
                return shortAlphaBeta ? "b" : "beta";
              case ReleaseCandidate:
                return "rc";
              }
              return "";
            } else {
              return std::to_string(pre);
            }
          },
          m_PreReleases[i]);
      if (!noSeparator && i < m_PreReleases.size() - 1) {
        value += ".";
      }
    }
  }

  if (!modes.testFlag(FormatMode::NoMetadata) && !m_BuildMetadata.isEmpty()) {
    value += "+" + m_BuildMetadata.toStdString();
  }

  return QString::fromStdString(value);
}

namespace
{
  // consume the given iterator until the given end iterator or until a non-zero value
  // is found
  //
  template <class It>
  It consumePreReleaseZeros(It it, It end)
  {
    for (; it != end; ++it) {
      if (!std::holds_alternative<int>(*it) != 0 || std::get<int>(*it) != 0) {
        break;
      }
    }
    return it;
  };
}  // namespace

std::strong_ordering operator<=>(const Version& lhs, const Version& rhs)
{
  auto mmp_cmp =
      std::forward_as_tuple(lhs.major(), lhs.minor(), lhs.patch(), lhs.subpatch()) <=>
      std::forward_as_tuple(rhs.major(), rhs.minor(), rhs.patch(), rhs.subpatch());

  // major.minor.patch have precedence over everything else
  if (mmp_cmp != std::strong_ordering::equal) {
    return mmp_cmp;
  }

  // handle cases were one is a pre-release and not the other - the pre-release is
  // "less" than the release
  if (lhs.isPreRelease() && !rhs.isPreRelease()) {
    return std::strong_ordering::less;
  }

  if (!lhs.isPreRelease() && rhs.isPreRelease()) {
    return std::strong_ordering::greater;
  }

  // compare pre-release fields
  auto lhsIt = lhs.preReleases().begin(), rhsIt = rhs.preReleases().begin();
  for (; lhsIt != lhs.preReleases().end() && rhsIt != rhs.preReleases().end();
       ++lhsIt, ++rhsIt) {

    const auto &lhsPre = *lhsIt, rhsPre = *rhsIt;

    // if one is alpha/beta/etc. and the other is numeric, the alpha/beta/etc. is lower
    // than the numeric one, which matches the index
    auto pre_cmp = lhsPre.index() <=> rhsPre.index();
    if (pre_cmp != std::strong_ordering::equal) {
      return pre_cmp;
    }

    // compare the actual values
    pre_cmp = lhsPre <=> rhsPre;
    if (pre_cmp != std::strong_ordering::equal) {
      return pre_cmp;
    }
  }

  // the code below does not follow semver 100% (I think) - basically, this makes stuff
  // like 2.4.1rc1.0 equals to 2.4.1rc1, which according to semver is probably not right
  // but is probably best for us
  //

  // if we land here, we have consumed one of the pre-release, we skip all the 0 in the
  // remaining one
  lhsIt = consumePreReleaseZeros(lhsIt, lhs.preReleases().end());
  rhsIt = consumePreReleaseZeros(rhsIt, rhs.preReleases().end());

  const auto lhsConsumed = lhsIt == lhs.preReleases().end(),
             rhsConsumed = rhsIt == rhs.preReleases().end();

  if (lhsConsumed && rhsConsumed) {
    return std::strong_ordering::equal;
  } else if (!lhsConsumed) {
    return std::strong_ordering::greater;
  } else {
    return std::strong_ordering::less;
  }
}

}  // namespace MOBase

#ifndef WINERUNTIMECONFIG_H
#define WINERUNTIMECONFIG_H

#include <QString>

#include <cstdint>

namespace WineRuntimeConfig {

enum class Source : std::uint8_t {
  None,
  InstanceExplicit,
  ApplicationDefault,
  InstanceLegacy,
};

struct Inputs {
  QString instanceIniPath;
  bool explicitPrefixPresent{false};
  QString explicitPrefix;
  bool explicitProtonPresent{false};
  QString explicitProton;
  QString defaultConfigPath;
  bool instanceConfigInvalid{false};
  bool defaultConfigPresent{false};
  bool defaultConfigInvalid{false};
  QString defaultPrefix;
  QString defaultProton;
  QString legacyPrefix;
  QString legacyProton;
};

struct FileIdentity {
  QString canonicalPath;
  std::uint64_t device{0};
  std::uint64_t inode{0};
  std::uint64_t size{0};
  std::int64_t modificationNanoseconds{0};
  std::int64_t changeNanoseconds{0};
  bool present{false};
};

struct Snapshot {
  QString prefixPath;
  QString compatDataPath;
  QString userProfilePath;
  QString protonPath;
  QString protonWinePath;
  QString prefixError;
  QString protonError;
  Source prefixSource{Source::None};
  Source protonSource{Source::None};
  std::uint64_t generation{0};
  FileIdentity prefixIdentity;
  FileIdentity driveIdentity;
  FileIdentity compatDataIdentity;
  FileIdentity userProfileIdentity;
  FileIdentity protonRootIdentity;
  FileIdentity protonWineIdentity;
  FileIdentity protonIdentity;

  bool valid() const noexcept {
    return prefixError.isEmpty() && protonError.isEmpty();
  }
};

// Pure resolution seam used by production and focused tests. Relative
// instance values are anchored at the selected instance INI; relative
// application defaults are anchored at config.json.
Snapshot resolve(const Inputs &inputs);

// Resolve one selected instance using its authoritative INI and the optional
// application-wide Fluorine default.
Snapshot resolveInstance(const QString &instanceIniPath);

// Publish an immutable setup-generation snapshot for bundled game plugins and
// every launch participant. Publishing is valid only while qApp exists.
bool publish(Snapshot snapshot);
Snapshot current();
void clear();
bool revalidatePrefix(const Snapshot &snapshot, QString *error = nullptr);
bool revalidateProton(const Snapshot &snapshot, QString *error = nullptr);
bool revalidate(const Snapshot &snapshot, QString *error = nullptr);

QString sourceName(Source source);

inline constexpr auto PrefixProperty = "fluorineWinePrefixPath";
inline constexpr auto CompatDataProperty = "fluorineWineCompatDataPath";
inline constexpr auto UserProfileProperty = "fluorineWineUserProfilePath";
inline constexpr auto ProtonProperty = "fluorineProtonPath";
inline constexpr auto PrefixErrorProperty = "fluorineWinePrefixError";
inline constexpr auto ProtonErrorProperty = "fluorineProtonError";
inline constexpr auto PrefixSourceProperty = "fluorineWinePrefixSource";
inline constexpr auto ProtonSourceProperty = "fluorineProtonSource";
inline constexpr auto GenerationProperty = "fluorineWineRuntimeGeneration";

} // namespace WineRuntimeConfig

#endif // WINERUNTIMECONFIG_H

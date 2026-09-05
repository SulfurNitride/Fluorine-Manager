#ifndef FUSEMOUNTOPTIONS_H
#define FUSEMOUNTOPTIONS_H

#include <QByteArray>
#include <string>
#include <vector>

inline constexpr auto kFuseAllowOtherSetting = "fluorine/fuse_allow_other";

inline bool fuseUserAllowOtherEnabled(const QByteArray& config)
{
  for (const auto& line : config.split('\n')) {
    if (line.split('#').first().trimmed() == "user_allow_other") {
      return true;
    }
  }
  return false;
}

inline std::vector<std::string> fuseMountArguments(bool allowOther)
{
  std::vector<std::string> args = {
      "mo2fuse", "-o", "fsname=mo2linux", "-o", "noatime",
      "-o", "max_read=1048576"};
  if (allowOther) {
    // The filesystem serves requests as the mounting user. Have the kernel
    // enforce inode ownership/modes before allowing access by other users.
    args.insert(args.end(), {"-o", "allow_other", "-o", "default_permissions"});
  }
  return args;
}

#endif

#include "permissionrepair.h"

#include <system_error>

namespace
{
namespace fs = std::filesystem;

void repairOne(const fs::path& path, PermissionRepairStats& stats)
{
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(path, ec);
  ++stats.inspected;
  if (ec) {
    ++stats.failed;
    return;
  }
  if (fs::is_symlink(status)) {
    ++stats.skipped;
    return;
  }

  fs::perms required = fs::perms::none;
  if (fs::is_directory(status)) {
    required = fs::perms::owner_read | fs::perms::owner_write |
               fs::perms::owner_exec;
  } else if (fs::is_regular_file(status)) {
    required = fs::perms::owner_read | fs::perms::owner_write;
  } else {
    ++stats.skipped;
    return;
  }

  if ((status.permissions() & required) == required) {
    ++stats.skipped;
    return;
  }

  fs::permissions(path, required, fs::perm_options::add, ec);
  if (ec) {
    ++stats.failed;
  } else {
    ++stats.repaired;
  }
}
}  // namespace

PermissionRepairOutcome permissionRepairOutcome(
    const PermissionRepairStats& stats) noexcept
{
  if (stats.failed != 0 || stats.traversal_error != 0) {
    return PermissionRepairOutcome::Failed;
  }
  if (stats.repaired != 0) {
    return PermissionRepairOutcome::RepairsApplied;
  }
  return PermissionRepairOutcome::NoChanges;
}

PermissionRepairStats repairGameDirectoryPermissions(
    const std::filesystem::path& game_directory)
{
  PermissionRepairStats stats;
  repairOne(game_directory, stats);

  std::error_code ec;
  fs::recursive_directory_iterator it(
      game_directory, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  while (!ec && it != end) {
    repairOne(it->path(), stats);
    it.increment(ec);
  }
  if (ec) {
    ++stats.failed;
    stats.traversal_error = ec.value();
  }
  return stats;
}

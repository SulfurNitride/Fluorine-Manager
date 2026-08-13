#ifndef UPDATERRESTARTPOLICY_H
#define UPDATERRESTARTPOLICY_H

#include <QByteArray>

#include <cstdint>
#include <optional>

#include "shared/exitstate.h"

namespace updater_restart
{

inline bool shouldRetryExit(ExitRequestResult result)
{
  return result == ExitRequestResult::Refused ||
         result == ExitRequestResult::InProgress;
}

inline bool helperMayLaunch(std::optional<std::uint64_t> observedStartTime,
                            std::uint64_t expectedStartTime, char processState)
{
  return !observedStartTime || *observedStartTime != expectedStartTime ||
         processState == 'Z' || processState == 'X' || processState == 'x';
}

inline QByteArray helperScript()
{
  return QByteArrayLiteral(
      "#!/usr/bin/env bash\n"
      "set -u\n"
      "OLD_PID=\"$1\"\n"
      "OLD_START=\"$2\"\n"
      "NEW_LAUNCHER=\"$3\"\n"
      "PROC_ROOT=\"${4:-/proc}\"\n"
      "CLEANUP_DIR=\"${5:-}\"\n"
      "old_process_is_same_generation() {\n"
      "  local STAT REST STATE START\n"
      "  [[ -e \"$PROC_ROOT/$OLD_PID/stat\" ]] || return 1\n"
      "  [[ -r \"$PROC_ROOT/$OLD_PID/stat\" ]] || return 0\n"
      "  IFS= read -r STAT < \"$PROC_ROOT/$OLD_PID/stat\" || {\n"
      "    [[ -e \"$PROC_ROOT/$OLD_PID/stat\" ]] && return 0 || return 1\n"
      "  }\n"
      "  REST=\"${STAT##*) }\"\n"
      "  [[ \"$REST\" != \"$STAT\" ]] || return 0\n"
      "  set -- $REST\n"
      "  STATE=\"${1:-}\"\n"
      "  START=\"${20:-}\"\n"
      "  [[ -n \"$START\" ]] || return 0\n"
      "  [[ \"$STATE\" != Z && \"$STATE\" != X && \"$STATE\" != x ]] "
      "|| return 1\n"
      "  [[ \"$START\" == \"$OLD_START\" ]]\n"
      "}\n"
      "while old_process_is_same_generation; do\n"
      "  sleep 0.1\n"
      "done\n"
      "if [[ -n \"$CLEANUP_DIR\" ]]; then\n"
      "  exec \"$NEW_LAUNCHER\" "
      "\"--fluorine-clean-update=$CLEANUP_DIR\" "
      "\"--fluorine-wait-publish=30\"\n"
      "fi\n"
      "exec \"$NEW_LAUNCHER\"\n");
}

}  // namespace updater_restart

#endif  // UPDATERRESTARTPOLICY_H

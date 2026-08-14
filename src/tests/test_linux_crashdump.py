#!/usr/bin/env python3

import os
from pathlib import Path
import subprocess
import sys
import time
import unittest


class LinuxCrashdumpTest(unittest.TestCase):
    def test_command_is_non_destructive_compatibility_tombstone(self) -> None:
        organizer = Path(sys.argv[1]).resolve()
        source_root = Path(sys.argv[2]).resolve()

        # Prove the destructive implementation is absent before executing the
        # compatibility command. This keeps the regression test itself safe on
        # developer hosts that may already have a real ModOrganizer process.
        env_cpp = (source_root / "src/src/env.cpp").read_text(encoding="utf-8")
        commandline_cpp = (source_root / "src/src/commandline.cpp").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("coredumpOther", env_cpp)
        self.assertNotIn("findOtherPid", env_cpp)
        self.assertNotIn("kill(pid, SIGABRT)", env_cpp)
        start = commandline_cpp.index("std::optional<int> CrashDumpCommand::runEarly()")
        end = commandline_cpp.index("Command::Meta LaunchCommand::meta()", start)
        tombstone = commandline_cpp[start:end]
        self.assertIn("not supported on Linux", tombstone)
        self.assertIn("return 1;", tombstone)
        for destructive in ("coredumpOther", "SIGABRT", "std::cin", "getchar(", "kill("):
            self.assertNotIn(destructive, tombstone)

        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = "offscreen"
        started = time.monotonic()
        result = subprocess.run(
            [str(organizer), "crashdump", "--type", "full"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            timeout=8,
            check=False,
            text=True,
        )
        elapsed = time.monotonic() - started

        self.assertNotEqual(result.returncode, 0)
        self.assertLess(elapsed, 8)
        message = (result.stdout + result.stderr).lower()
        self.assertIn("not supported on linux", message)
        self.assertIn("no process was signalled", message)
        self.assertIn("coredumpctl", message)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])

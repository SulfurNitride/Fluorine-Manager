import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "src"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class ProfileTweakSurfaceTest(unittest.TestCase):
    def test_profile_publishes_one_authoritative_generation(self):
        profile = (SRC / "profile.cpp").read_text(encoding="utf-8")
        create = function_body(profile, "bool Profile::createTweakedIniFile")
        self.assertIn("ProfileTweakMerge::publish", create)
        self.assertNotIn("shellDelete", create)
        self.assertNotIn("WriteRegistryValue", create)
        self.assertNotIn("mergeTweak(", profile)

    def test_launch_aborts_if_tweaked_ini_cannot_be_published(self):
        core = (SRC / "organizercore.cpp").read_text(encoding="utf-8")
        before = function_body(core, "bool OrganizerCore::beforeRun")
        self.assertIn("currentProfileTweaksSaved = saveCurrentProfile()", before)
        self.assertIn("launchProfile->createTweakedIniFile()", before)
        self.assertIn("if (!launchProfileTweaksSaved)", before)
        self.assertLess(
            before.index("if (!launchProfileTweaksSaved)"),
            before.index("m_AboutToRun"),
        )


if __name__ == "__main__":
    unittest.main()

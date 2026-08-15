import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
CORE = ROOT / "src" / "src" / "organizercore.cpp"


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


class PandoraPreviousOutputSurfaceTest(unittest.TestCase):
    def test_launch_uses_checked_atomic_normalization(self):
        before = function_body(
            CORE.read_text(encoding="utf-8"), "bool OrganizerCore::beforeRun"
        )
        self.assertIn("PandoraPreviousOutput::normalize", before)
        self.assertIn("if (!normalized)", before)
        self.assertNotIn("QIODevice::Truncate", before)
        self.assertLess(
            before.index("PandoraPreviousOutput::normalize"),
            before.index("checkGameRegistryKey"),
        )


if __name__ == "__main__":
    unittest.main()

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ArchiveOutputSurfaceTests(unittest.TestCase):
    def test_callback_preflights_the_complete_batch_before_mutation(self):
        source = (ROOT / "libs/archive/src/extractcallback.cpp").read_text()
        begin = source.index("STDMETHODIMP CArchiveExtractCallback::GetStream")
        end = source.index(
            "STDMETHODIMP CArchiveExtractCallback::PrepareOperation", begin
        )
        get_stream = source[begin:end]

        validation = get_stream.index("archive_output::validateAll")
        self.assertLess(validation, get_stream.index("create_directories"))
        self.assertLess(validation, get_stream.index("fs::remove"))
        self.assertLess(validation, get_stream.index("MultiOutputStream"))

    def test_archive_target_compiles_the_confinement_helper(self):
        cmake = (ROOT / "libs/archive/src/CMakeLists.txt").read_text()
        self.assertIn("archiveoutputpath.cpp", cmake)
        self.assertIn("archiveoutputpath.h", cmake)


if __name__ == "__main__":
    unittest.main()

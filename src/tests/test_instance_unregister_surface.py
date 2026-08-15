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


class InstanceUnregisterSurfaceTest(unittest.TestCase):
    def test_dialog_refreshes_only_after_durable_removal(self):
        source = (SRC / "instancemanagerdialog.cpp").read_text(encoding="utf-8")
        remove = function_body(source, "void InstanceManagerDialog::removeFromList")
        self.assertIn("InstanceUnregister::disableGlobalIni", remove)
        self.assertIn("InstanceManager::unregisterPortableInstance", remove)
        self.assertNotIn("QFile::rename", remove)
        self.assertIn("built-in portable instance", remove)
        self.assertIn("rename it back manually", remove)
        self.assertLess(remove.index("if (!disabled)"), remove.index("updateInstances"))
        self.assertLess(
            remove.index("if (!InstanceManager::unregisterPortableInstance"),
            remove.index("updateInstances"),
        )

        rename = function_body(source, "void InstanceManagerDialog::rename")
        self.assertIn("replacePortableInstance", rename)
        self.assertIn("directory rename was rolled back", rename)
        delete = function_body(source, "void InstanceManagerDialog::deleteInstance")
        self.assertIn("!InstanceManager::singleton().unregisterPortableInstance", delete)
        open_existing = function_body(
            source, "void InstanceManagerDialog::openExistingPortable"
        )
        self.assertIn("if (!InstanceManager::registerPortableInstance", open_existing)
        self.assertIn("instance_path::sameDirectoryOrPath", open_existing)

        update = function_body(source, "void InstanceManagerDialog::updateInstances")
        self.assertIn("instance_path::sameDirectoryOrPath", update)
        self.assertIn("addedPortablePaths", update)

        create = (SRC / "createinstancedialog.cpp").read_text(encoding="utf-8")
        finish = function_body(create, "void CreateInstanceDialog::finish")
        self.assertIn("!InstanceManager::singleton().registerPortableInstance", finish)
        self.assertLess(
            finish.index("!InstanceManager::singleton().registerPortableInstance"),
            finish.index("d->commit()"),
        )

    def test_registry_mutations_sync_and_return_results(self):
        settings = (SRC / "settings.cpp").read_text(encoding="utf-8")
        add = function_body(settings, "bool GlobalSettings::addPortableInstance")
        remove = function_body(settings, "bool GlobalSettings::removePortableInstance")
        for body in (add, remove):
            self.assertIn("updatePortableRegistration", body)
            self.assertIn("return admitted && saved", body)
            self.assertIn("RegistryStatus::RollbackFailed", body)
            self.assertIn("backend.release()", body)

        replace = function_body(
            settings, "bool GlobalSettings::replacePortableInstance"
        )
        self.assertIn("replacePortableRegistration", replace)
        self.assertIn("return admitted && saved", replace)

        manager = (SRC / "instancemanager.cpp").read_text(encoding="utf-8")
        unregister = function_body(
            manager, "bool InstanceManager::unregisterPortableInstance"
        )
        self.assertIn("return GlobalSettings::removePortableInstance", unregister)


if __name__ == "__main__":
    unittest.main()

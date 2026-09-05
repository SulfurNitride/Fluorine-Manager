import json
import unittest
from datetime import date
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CATALOG = (
    ROOT / "src" / "src" / "resources" / "wabbajack-compatibility" / "catalog.json"
)
INVENTORY = ROOT / "docs" / "modlist-compatibility-inventory.json"


class CompatibilityCatalogTest(unittest.TestCase):
    def test_catalog_references_reviewed_nexus_tools(self):
        catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
        self.assertEqual(catalog["schemaVersion"], 2)
        self.assertEqual(catalog["policy"], "reviewed-declarative-actions-only")

        tools = {tool["id"]: tool for tool in catalog["tools"]}
        self.assertEqual(tools["fnv-4gb-patcher-linux"]["source"], "nexus")
        self.assertEqual(tools["fnv-4gb-patcher-linux"]["modId"], 62552)
        self.assertEqual(
            tools["fnv-4gb-patcher-linux"]["fileNameContains"], "proton"
        )
        self.assertEqual(
            tools["fnv-4gb-patcher-linux"]["successFile"],
            "FalloutNV_backup.exe",
        )
        oblivion_patcher = tools["oblivion-4gb-patcher-linux"]
        self.assertEqual(oblivion_patcher["source"], "nexus")
        self.assertEqual(oblivion_patcher["domain"], "oblivion")
        self.assertEqual(oblivion_patcher["modId"], 56555)
        self.assertEqual(oblivion_patcher["executable"], "4gb-patch")
        self.assertEqual(oblivion_patcher["minimumVersion"], "1")
        self.assertEqual(tools["native-mpi-installer"]["source"], "nexus")
        self.assertEqual(tools["native-mpi-installer"]["modId"], 1657)
        self.assertEqual(tools["fnv-bsa-decompressor-mpi"]["source"], "nexus")
        self.assertEqual(tools["fnv-bsa-decompressor-mpi"]["modId"], 65854)

        profiles = {
            profile["machineName"]: profile for profile in catalog["profiles"]
        }
        vnv = profiles["VivaNewVegas"]
        self.assertTrue(vnv["implemented"])
        self.assertEqual(vnv["executable"], "FalloutNV.exe")
        self.assertEqual(vnv["patcher"], "fnv-nexus-linux")
        self.assertEqual(vnv["gameRoot"], "original")
        self.assertNotIn("create-verified-stock-game", vnv["actions"])
        self.assertIn("patch-falloutnv-with-nexus-linux-patcher", vnv["actions"])
        for action in vnv["optionalActions"]:
            self.assertIn(action["runner"], tools)
            self.assertIn(action["artifact"], tools)
            # Optional native operations remain disabled until their exact Nexus
            # release and output contract have been exercised end-to-end.
            self.assertEqual(action["state"], "review-before-enabling")

        waters = profiles["watersoflife"]
        self.assertTrue(waters["implemented"])
        self.assertEqual(waters["game"], "fallout3")
        self.assertEqual(waters["executable"], "Fallout3.exe")
        self.assertEqual(waters["gameRoot"], "original")
        self.assertIn(
            "__Files Requiring Manual Install", waters["manualRootFolders"]
        )

        heartland = profiles["heartlandredux"]
        self.assertTrue(heartland["implemented"])
        self.assertEqual(heartland["game"], "oblivion")
        self.assertEqual(heartland["patcher"], "oblivion-nexus-linux")
        self.assertEqual(heartland["gameRoot"], "original")
        self.assertIn("Game Folder Files", heartland["manualRootFolders"])
        self.assertIn(
            "patch-oblivion-with-nexus-linux-patcher", heartland["actions"]
        )

        # Fluorine follows the modlist author's game-root architecture. It must
        # never invent a Stock Game for a list that documents game-folder files.
        self.assertNotIn("fotw", profiles)
        self.assertFalse(profiles["Morrowind75"]["implemented"])
        for profile in profiles.values():
            if not profile.get("implemented"):
                continue
            self.assertIn(profile["gameRoot"], {"original", "authored-stock-game"})
            self.assertNotIn("create-verified-stock-game", profile.get("actions", []))
            if profile["gameRoot"] == "authored-stock-game":
                self.assertTrue(profile.get("stockGameFolder"))

    def test_adapter_inventory_enforces_maintenance_window(self):
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        policy = inventory["candidate_policy"]
        cutoff = date.fromisoformat(policy["activity_cutoff"])
        self.assertEqual(policy["maintenance_window_months"], 12)
        exceptions = {
            candidate["machine_name"].lower()
            for candidate in inventory["maintenance_exceptions"]
        }

        active_names = set()
        for candidate in inventory["adapter_candidates"]:
            active_names.add(candidate["machine_name"].lower())
            if candidate["machine_name"].lower() not in exceptions:
                self.assertGreaterEqual(
                    date.fromisoformat(candidate["last_activity"]), cutoff
                )

        stale_names = {
            candidate["machine_name"].lower()
            for candidate in inventory["excluded_stale_candidates"]
        }
        for candidate in inventory["excluded_stale_candidates"]:
            self.assertLess(date.fromisoformat(candidate["last_activity"]), cutoff)
        self.assertEqual(stale_names, {"fotw"})
        self.assertTrue(active_names.isdisjoint(stale_names))


if __name__ == "__main__":
    unittest.main()

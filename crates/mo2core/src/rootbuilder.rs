//! Root Builder utilities — scanning and detection for mods with Root/ folders.
//!
//! Root/ folders in mods contain files that go to the game's root directory
//! (where .exe lives), NOT the Data/ folder. Examples: SKSE, ENB, engine fixes.
//!
//! With the full-game VFS approach, Root/ files are served as VFS layers
//! directly — no file copying or symlinking needed. This module provides
//! scanning utilities for the UI (detecting which mods have Root/ content,
//! validating Root/ folder structure, etc.).

use std::path::Path;

use crate::instance::Instance;
use crate::modinfo::ModType;

// ── Folders/extensions that belong in Data, NOT game root ────────────

const DATA_FOLDERS: &[&str] = &[
    "meshes",
    "textures",
    "skse",
    "f4se",
    "nvse",
    "fose",
    "obse",
    "mwse",
    "sfse",
    "icons",
    "materials",
    "scripts",
    "music",
    "sound",
    "shaders",
    "video",
    "fonts",
    "menus",
    "splash",
    "interface",
    "seq",
    "strings",
    "grass",
    "lodsettings",
    "distantlod",
    "asi",
    "tools",
    "mcm",
    "dialogueviews",
];

const DATA_EXTENSIONS: &[&str] = &["esp", "esm", "esl", "bsa", "ba2"];

/// If a mod's Root/ folder contains any of these, the entire Root/ is invalid.
const INVALID_ROOT_CONTENTS: &[&str] = &["data", "data files", "fomod"];

// ── Root Builder scanning ────────────────────────────────────────────

/// Check if any enabled mods have Root/ folders.
pub fn has_root_mods(instance: &Instance) -> bool {
    instance
        .mods
        .iter()
        .any(|m| m.enabled && m.mod_type == ModType::Regular && m.path.join("Root").is_dir())
}

/// Get a list of mod names that have Root/ folders.
pub fn mods_with_root(instance: &Instance) -> Vec<&str> {
    instance
        .mods
        .iter()
        .filter(|m| m.enabled && m.mod_type == ModType::Regular && m.path.join("Root").is_dir())
        .map(|m| m.name.as_str())
        .collect()
}

/// List files in a mod's Root/ folder (relative to Root/).
pub fn list_root_files(mod_path: &Path) -> Vec<String> {
    let root_dir = mod_path.join("Root");
    if !root_dir.is_dir() {
        return Vec::new();
    }

    let mut files = Vec::new();
    for entry in walkdir::WalkDir::new(&root_dir)
        .into_iter()
        .filter_map(|e| e.ok())
    {
        if !entry.file_type().is_file() {
            continue;
        }
        if let Ok(relative) = entry.path().strip_prefix(&root_dir) {
            let rel_str = relative.to_string_lossy().to_string();
            if !rel_str.is_empty() {
                files.push(rel_str);
            }
        }
    }
    files
}

/// Check if a mod's Root/ folder contains invalid subdirectories
/// (Data/, fomod/, etc.). If so, the Root/ folder should be ignored.
pub fn has_invalid_root_contents(mod_path: &Path) -> bool {
    let root_dir = mod_path.join("Root");
    if !root_dir.is_dir() {
        return false;
    }

    if let Ok(entries) = std::fs::read_dir(&root_dir) {
        for entry in entries.flatten() {
            if entry.file_type().map(|t| t.is_dir()).unwrap_or(false) {
                let name = entry.file_name().to_string_lossy().to_lowercase();
                if INVALID_ROOT_CONTENTS.contains(&name.as_str()) {
                    return true;
                }
            }
        }
    }
    false
}

/// Check if a file relative path looks like it belongs in Data/ rather than Root/.
///
/// Used for UI warnings when a mod's Root/ contains misplaced files.
pub fn is_data_content(relative_path: &str) -> bool {
    let lower = relative_path.to_lowercase();
    let first_component = lower.split('/').next().unwrap_or("");

    if DATA_FOLDERS.contains(&first_component) {
        return true;
    }

    if let Some(ext) = Path::new(relative_path)
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| e.to_lowercase())
    {
        if DATA_EXTENSIONS.contains(&ext.as_str()) {
            return true;
        }
    }

    false
}

/// If an executable path is inside a mod's Root/ folder, return the
/// relative path within the game root (for redirecting to VFS mount).
pub fn redirect_executable(exe_path: &Path, instance: &Instance) -> Option<String> {
    let active = instance.active_mods_sorted();

    for (_mod_name, mod_path) in &active {
        let root_dir = mod_path.join("Root");
        if let Ok(relative) = exe_path.strip_prefix(&root_dir) {
            return Some(relative.to_string_lossy().to_string());
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::instance::create_instance;

    fn make_test_instance(tmp: &Path) -> Instance {
        let root = tmp.join("instance");
        create_instance(&root, "Skyrim Special Edition").unwrap();

        let mods_dir = root.join("mods");

        // Mod with Root/
        let mod_a = mods_dir.join("SKSE");
        std::fs::create_dir_all(mod_a.join("Root")).unwrap();
        std::fs::write(mod_a.join("Root/skse64_loader.exe"), "skse").unwrap();
        std::fs::write(mod_a.join("Root/skse64_1_5_97.dll"), "dll").unwrap();

        // Mod with Root/ + regular content
        let mod_b = mods_dir.join("ENB");
        std::fs::create_dir_all(mod_b.join("Root")).unwrap();
        std::fs::write(mod_b.join("Root/d3d11.dll"), "enb").unwrap();
        std::fs::write(mod_b.join("textures/enb_tex.dds"), "tex").unwrap_or(());
        std::fs::create_dir_all(mod_b.join("textures")).unwrap();
        std::fs::write(mod_b.join("textures/enb_tex.dds"), "tex").unwrap();

        // Mod without Root/
        let mod_c = mods_dir.join("TextureMod");
        std::fs::create_dir_all(mod_c.join("textures")).unwrap();
        std::fs::write(mod_c.join("textures/sky.dds"), "tex").unwrap();

        // Mod with invalid Root/
        let mod_d = mods_dir.join("BadMod");
        std::fs::create_dir_all(mod_d.join("Root/Data")).unwrap();
        std::fs::write(mod_d.join("Root/Data/test.esp"), "bad").unwrap();

        // Set up modlist
        let modlist = "# This file was automatically generated by Mod Organizer.\r\n\
            +SKSE\r\n\
            +ENB\r\n\
            +TextureMod\r\n\
            +BadMod\r\n";
        std::fs::write(root.join("profiles/Default/modlist.txt"), modlist).unwrap();

        Instance::load(&root).unwrap()
    }

    #[test]
    fn test_has_root_mods() {
        let tmp = tempfile::tempdir().unwrap();
        let instance = make_test_instance(tmp.path());
        assert!(has_root_mods(&instance));
    }

    #[test]
    fn test_mods_with_root() {
        let tmp = tempfile::tempdir().unwrap();
        let instance = make_test_instance(tmp.path());
        let root_mods = mods_with_root(&instance);

        assert!(root_mods.contains(&"SKSE"));
        assert!(root_mods.contains(&"ENB"));
        assert!(root_mods.contains(&"BadMod")); // has Root/ even if invalid
        assert!(!root_mods.contains(&"TextureMod"));
    }

    #[test]
    fn test_list_root_files() {
        let tmp = tempfile::tempdir().unwrap();
        let instance = make_test_instance(tmp.path());

        let skse_mod = instance.find_mod("SKSE").unwrap();
        let files = list_root_files(&skse_mod.path);
        assert_eq!(files.len(), 2);
        assert!(files.iter().any(|f| f == "skse64_loader.exe"));
        assert!(files.iter().any(|f| f == "skse64_1_5_97.dll"));
    }

    #[test]
    fn test_list_root_files_no_root() {
        let tmp = tempfile::tempdir().unwrap();
        let instance = make_test_instance(tmp.path());

        let tex_mod = instance.find_mod("TextureMod").unwrap();
        let files = list_root_files(&tex_mod.path);
        assert!(files.is_empty());
    }

    #[test]
    fn test_invalid_root_contents() {
        let tmp = tempfile::tempdir().unwrap();
        let instance = make_test_instance(tmp.path());

        let bad = instance.find_mod("BadMod").unwrap();
        assert!(has_invalid_root_contents(&bad.path));

        let good = instance.find_mod("SKSE").unwrap();
        assert!(!has_invalid_root_contents(&good.path));

        let no_root = instance.find_mod("TextureMod").unwrap();
        assert!(!has_invalid_root_contents(&no_root.path));
    }

    #[test]
    fn test_is_data_content() {
        // These belong in Data/, not Root/
        assert!(is_data_content("textures/armor.dds"));
        assert!(is_data_content("meshes/body.nif"));
        assert!(is_data_content("scripts/script.pex"));
        assert!(is_data_content("mymod.esp"));
        assert!(is_data_content("archive.bsa"));

        // These are valid Root/ content
        assert!(!is_data_content("skse64_loader.exe"));
        assert!(!is_data_content("d3d11.dll"));
        assert!(!is_data_content("enbseries.ini"));
        assert!(!is_data_content("readme.txt"));
    }

    #[test]
    fn test_redirect_executable() {
        let tmp = tempfile::tempdir().unwrap();
        let instance = make_test_instance(tmp.path());

        // Path inside a mod's Root/
        let exe = instance.mods_dir().join("SKSE/Root/skse64_loader.exe");
        let redirected = redirect_executable(&exe, &instance);
        assert_eq!(redirected, Some("skse64_loader.exe".to_string()));

        // Path not inside any Root/
        let other = std::path::PathBuf::from("/some/random/path.exe");
        assert!(redirect_executable(&other, &instance).is_none());
    }
}

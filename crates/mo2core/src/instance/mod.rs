//! MO2 instance management.
//!
//! An instance is a complete MO2 setup for a specific game, containing:
//! - ModOrganizer.ini (main config)
//! - mods/ directory
//! - profiles/ directory
//! - downloads/ directory
//! - overwrite/ directory

use std::path::{Path, PathBuf};

use crate::categories::Categories;
use crate::config::organizer_ini::OrganizerIni;
use crate::global_settings::GlobalSettings;
use crate::modinfo::ModInfo;
use crate::plugin_list::PluginList;
use crate::profile::Profile;

/// An MO2 instance (one game's complete mod setup).
#[derive(Debug)]
pub struct Instance {
    /// Root directory of this instance
    pub root: PathBuf,
    /// Parsed ModOrganizer.ini
    pub config: OrganizerIni,
    /// Currently active profile
    pub active_profile: Option<Profile>,
    /// All discovered mods
    pub mods: Vec<ModInfo>,
    /// Category definitions
    pub categories: Categories,
}

impl Instance {
    /// Load an instance from its root directory.
    pub fn load(root: &Path) -> anyhow::Result<Self> {
        let config_path = root.join("ModOrganizer.ini");
        if !config_path.exists() {
            anyhow::bail!(
                "Not an MO2 instance: ModOrganizer.ini not found in {:?}",
                root
            );
        }

        let config = OrganizerIni::read(&config_path)?;

        // Load categories
        let cats_path = root.join("categories.dat");
        let categories = if cats_path.exists() {
            Categories::read(&cats_path)?
        } else {
            Categories::default()
        };

        let mut instance = Instance {
            root: root.to_path_buf(),
            config,
            active_profile: None,
            mods: Vec::new(),
            categories,
        };

        // Load mods
        instance.refresh_mods()?;

        // Load active profile (fallback to first available profile if selection is invalid/missing)
        let selected = instance.config.selected_profile().map(ToString::to_string);
        let profile_to_load = if let Some(profile_name) = selected {
            let p = instance.profiles_dir().join(&profile_name);
            if p.exists() {
                Some((profile_name, p))
            } else {
                None
            }
        } else {
            None
        }
        .or_else(|| {
            crate::profile::list_profiles(&instance.profiles_dir())
                .ok()
                .and_then(|mut names| {
                    names.sort();
                    names
                        .into_iter()
                        .next()
                        .map(|name| (name.clone(), instance.profiles_dir().join(name)))
                })
        });

        if let Some((profile_name, profile_path)) = profile_to_load {
            let profile = Profile::load(&profile_path)?;
            instance.apply_profile_to_mods(&profile);
            instance.active_profile = Some(profile);
            instance.config.set_selected_profile(&profile_name);
        }

        Ok(instance)
    }

    /// Get the mods directory.
    pub fn mods_dir(&self) -> PathBuf {
        self.config
            .base_directory()
            .unwrap_or_else(|| self.root.join("mods"))
    }

    /// Get the profiles directory.
    pub fn profiles_dir(&self) -> PathBuf {
        self.config
            .profiles_directory()
            .unwrap_or_else(|| self.root.join("profiles"))
    }

    /// Get the downloads directory.
    pub fn downloads_dir(&self) -> PathBuf {
        self.config
            .download_directory()
            .unwrap_or_else(|| self.root.join("downloads"))
    }

    /// Get the overwrite directory.
    pub fn overwrite_dir(&self) -> PathBuf {
        self.config
            .overwrite_directory()
            .unwrap_or_else(|| self.root.join("overwrite"))
    }

    /// Get the game name.
    pub fn game_name(&self) -> Option<&str> {
        self.config.game_name()
    }

    /// Refresh the list of mods from the mods directory.
    pub fn refresh_mods(&mut self) -> anyhow::Result<()> {
        self.mods.clear();
        let mods_dir = self.mods_dir();

        if !mods_dir.exists() {
            return Ok(());
        }

        for entry in std::fs::read_dir(&mods_dir)? {
            let entry = entry?;
            if entry.file_type()?.is_dir() {
                match ModInfo::from_directory(&entry.path()) {
                    Ok(mut mod_info) => {
                        if let Err(e) = mod_info.scan_contents() {
                            tracing::warn!("Failed to scan contents for {:?}: {}", entry.path(), e);
                        }
                        self.mods.push(mod_info);
                    }
                    Err(e) => {
                        tracing::warn!("Failed to load mod {:?}: {}", entry.path(), e);
                    }
                }
            }
        }

        // Add overwrite
        let overwrite_dir = self.overwrite_dir();
        if overwrite_dir.exists() {
            self.mods
                .push(crate::modinfo::create_overwrite(&overwrite_dir));
        }

        Ok(())
    }

    /// Apply a profile's mod list to the loaded mods.
    fn apply_profile_to_mods(&mut self, profile: &Profile) {
        // Remove synthetic separators from previous profiles.
        self.mods
            .retain(|m| !(m.is_separator() && !m.path.exists()));

        // Windows MO2 stores separators in modlist.txt as `_separator_<Name>`
        // without requiring a physical folder in `mods/`.
        for entry in &profile.modlist.entries {
            if !crate::config::modlist::ModList::is_separator(&entry.name) {
                continue;
            }
            if self
                .mods
                .iter()
                .any(|m| m.name.eq_ignore_ascii_case(&entry.name))
            {
                continue;
            }

            self.mods.push(ModInfo {
                name: entry.name.clone(),
                path: self.mods_dir().join(&entry.name),
                mod_type: crate::modinfo::ModType::Separator,
                enabled: entry.status == crate::config::modlist::ModStatus::Enabled,
                priority: entry.priority,
                meta: None,
                content_types: Vec::new(),
                flags: Vec::new(),
            });
        }

        let mod_count = self.mods.len() as i32;
        for mod_info in &mut self.mods {
            if let Some(entry) = profile.modlist.find(&mod_info.name) {
                mod_info.enabled = entry.status == crate::config::modlist::ModStatus::Enabled;
                mod_info.priority = entry.priority;
            } else if mod_info.is_overwrite() {
                mod_info.enabled = true;
                mod_info.priority = mod_count;
            } else {
                mod_info.enabled = false;
                mod_info.priority = -1;
            }
        }
    }

    /// Switch to a different profile.
    pub fn set_active_profile(&mut self, name: &str) -> anyhow::Result<()> {
        let profile_path = self.profiles_dir().join(name);
        if !profile_path.exists() {
            anyhow::bail!("Profile '{}' does not exist", name);
        }

        let profile = Profile::load(&profile_path)?;
        self.apply_profile_to_mods(&profile);
        self.active_profile = Some(profile);

        // Update config
        self.config.set_selected_profile(name);
        self.config.write(&self.root.join("ModOrganizer.ini"))?;

        Ok(())
    }

    /// List available profiles.
    pub fn list_profiles(&self) -> anyhow::Result<Vec<String>> {
        crate::profile::list_profiles(&self.profiles_dir())
    }

    /// Get active mods sorted by priority (for VFS building).
    /// Returns (mod_name, mod_path) pairs, lowest priority first.
    pub fn active_mods_sorted(&self) -> Vec<(&str, &Path)> {
        let mut active: Vec<&ModInfo> = self
            .mods
            .iter()
            .filter(|m| m.enabled && m.priority >= 0)
            .collect();
        active.sort_by_key(|m| m.priority);
        active
            .into_iter()
            .map(|m| (m.name.as_str(), m.path.as_path()))
            .collect()
    }

    /// Build the plugin list from the active profile and mod directories.
    ///
    /// Also scans the game's Data directory for base plugins (.esm, .esp, .esl)
    /// so they appear in the plugin list alongside mod plugins.
    pub fn build_plugin_list(&self) -> PluginList {
        let Some(ref profile) = self.active_profile else {
            return PluginList::new();
        };

        let mut mod_dirs: Vec<(String, PathBuf, bool)> = Vec::new();
        let game_def = self.config.game_directory().and_then(|game_dir| {
            crate::gamedef::GameDef::from_instance(self.game_name(), Some(&game_dir))
        });

        // Add the game's Data directory as the base layer (always enabled)
        if let Some(game_dir) = self.config.game_directory() {
            let data_dir_name = game_def
                .as_ref()
                .map(|gd| gd.data_dir_name.clone())
                .unwrap_or_else(|| "Data".to_string());
            let data_dir = game_dir.join(&data_dir_name);
            if data_dir.exists() {
                mod_dirs.push(("<Game>".to_string(), data_dir, true));
            }
        }

        // Add mod directories
        for m in &self.mods {
            mod_dirs.push((m.name.clone(), m.path.clone(), m.enabled));
        }

        PluginList::build(
            &profile.plugins,
            &profile.load_order,
            &profile.locked_order,
            &game_def
                .as_ref()
                .map(|gd| gd.primary_plugins())
                .unwrap_or_default(),
            &mod_dirs,
        )
    }

    /// Scan all enabled mods for BSA/BA2 archive files.
    /// Returns (archive_filename, origin_mod_name, mod_enabled) tuples.
    pub fn scan_archives(&self) -> Vec<(String, String, bool)> {
        let mut archives = Vec::new();

        for mod_info in &self.mods {
            if mod_info.is_overwrite() {
                continue;
            }
            if !mod_info.path.exists() {
                continue;
            }

            // Only scan top level for .bsa/.ba2 files
            if let Ok(entries) = std::fs::read_dir(&mod_info.path) {
                for entry in entries.flatten() {
                    let path = entry.path();
                    if !path.is_file() {
                        continue;
                    }
                    let ext = path
                        .extension()
                        .and_then(|e| e.to_str())
                        .map(|e| e.to_lowercase())
                        .unwrap_or_default();
                    if ext == "bsa" || ext == "ba2" {
                        let filename = path
                            .file_name()
                            .and_then(|n| n.to_str())
                            .unwrap_or("")
                            .to_string();
                        archives.push((filename, mod_info.name.clone(), mod_info.enabled));
                    }
                }
            }
        }

        archives.sort_by(|a, b| a.0.to_lowercase().cmp(&b.0.to_lowercase()));
        archives
    }

    /// Find a mod by name (case-insensitive).
    pub fn find_mod(&self, name: &str) -> Option<&ModInfo> {
        let lower = name.to_lowercase();
        self.mods.iter().find(|m| m.name.to_lowercase() == lower)
    }

    /// Find a mod by name (case-insensitive, mutable).
    pub fn find_mod_mut(&mut self, name: &str) -> Option<&mut ModInfo> {
        let lower = name.to_lowercase();
        self.mods
            .iter_mut()
            .find(|m| m.name.to_lowercase() == lower)
    }
}

/// Summary information about an instance (for listing without fully loading).
#[derive(Debug, Clone)]
pub struct InstanceInfo {
    pub name: String,
    pub path: PathBuf,
    pub game_name: String,
    pub is_portable: bool,
}

/// Check if a directory is a portable MO2 instance.
/// Portable instances have `portable.txt` alongside `ModOrganizer.ini`.
pub fn detect_portable(path: &Path) -> bool {
    path.join("portable.txt").exists() && path.join("ModOrganizer.ini").exists()
}

/// Get basic info about a portable instance without fully loading it.
pub fn portable_instance_info(path: &Path) -> anyhow::Result<InstanceInfo> {
    let config_path = path.join("ModOrganizer.ini");
    if !config_path.exists() {
        anyhow::bail!(
            "Not an MO2 instance: ModOrganizer.ini not found in {:?}",
            path
        );
    }

    let config = OrganizerIni::read(&config_path)?;
    let game_name = config.game_name().unwrap_or("Unknown").to_string();
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| "Unknown".to_string());

    Ok(InstanceInfo {
        name,
        path: path.to_path_buf(),
        game_name,
        is_portable: detect_portable(path),
    })
}

/// Scan the global instances directory (`~/.local/share/MO2Linux/`) for instances.
pub fn list_global_instances() -> anyhow::Result<Vec<InstanceInfo>> {
    let root = GlobalSettings::global_instances_root();
    if !root.exists() {
        return Ok(Vec::new());
    }

    let mut instances = Vec::new();
    for entry in std::fs::read_dir(&root)? {
        let entry = entry?;
        if !entry.file_type()?.is_dir() {
            continue;
        }
        let dir = entry.path();
        if !dir.join("ModOrganizer.ini").exists() {
            continue;
        }
        match portable_instance_info(&dir) {
            Ok(mut info) => {
                info.is_portable = false; // Global instances are not portable
                instances.push(info);
            }
            Err(e) => {
                tracing::warn!("Failed to read instance at {:?}: {}", dir, e);
            }
        }
    }

    instances.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(instances)
}

/// Create a new global instance in `~/.local/share/MO2Linux/<name>/`.
pub fn create_global_instance(name: &str, game_name: &str) -> anyhow::Result<Instance> {
    let root = GlobalSettings::global_instances_root().join(name);
    if root.exists() {
        anyhow::bail!("Instance '{}' already exists at {:?}", name, root);
    }
    create_instance(&root, game_name)
}

/// Create a new portable instance at the given root directory.
/// Writes a `portable.txt` marker file alongside the standard instance.
pub fn create_portable_instance(root: &Path, game_name: &str) -> anyhow::Result<Instance> {
    let instance = create_instance(root, game_name)?;
    // Write portable.txt marker
    std::fs::write(root.join("portable.txt"), "")?;
    Ok(instance)
}

/// Create a new MO2 instance.
pub fn create_instance(root: &Path, game_name: &str) -> anyhow::Result<Instance> {
    std::fs::create_dir_all(root)?;
    std::fs::create_dir_all(root.join("mods"))?;
    std::fs::create_dir_all(root.join("profiles"))?;
    std::fs::create_dir_all(root.join("downloads"))?;
    std::fs::create_dir_all(root.join("overwrite"))?;

    let mut config = OrganizerIni::parse("");
    config.set_game_name(game_name);
    config.set_selected_profile("Default");
    config.write(&root.join("ModOrganizer.ini"))?;

    // Create default profile
    Profile::create_new(&root.join("profiles"), "Default")?;

    Instance::load(root)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_create_and_load_instance() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("test_instance");

        let instance = create_instance(&root, "Skyrim Special Edition").unwrap();
        assert_eq!(instance.game_name(), Some("Skyrim Special Edition"));
        assert!(instance.active_profile.is_some());
        assert_eq!(instance.active_profile.as_ref().unwrap().name, "Default");
    }

    #[test]
    fn test_instance_directories() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("test_instance");
        let instance = create_instance(&root, "Skyrim").unwrap();

        assert_eq!(instance.mods_dir(), root.join("mods"));
        assert_eq!(instance.profiles_dir(), root.join("profiles"));
        assert_eq!(instance.downloads_dir(), root.join("downloads"));
        assert_eq!(instance.overwrite_dir(), root.join("overwrite"));
    }

    #[test]
    fn test_list_profiles() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("test_instance");
        let instance = create_instance(&root, "Skyrim").unwrap();

        let profiles = instance.list_profiles().unwrap();
        assert_eq!(profiles, vec!["Default"]);
    }

    #[test]
    fn test_detect_portable() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("portable_instance");
        create_instance(&root, "Skyrim").unwrap();

        // Not portable yet
        assert!(!detect_portable(&root));

        // Add portable.txt
        std::fs::write(root.join("portable.txt"), "").unwrap();
        assert!(detect_portable(&root));
    }

    #[test]
    fn test_portable_instance_info() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("my_instance");
        create_instance(&root, "Fallout 4").unwrap();
        std::fs::write(root.join("portable.txt"), "").unwrap();

        let info = portable_instance_info(&root).unwrap();
        assert_eq!(info.name, "my_instance");
        assert_eq!(info.game_name, "Fallout 4");
        assert!(info.is_portable);
    }

    #[test]
    fn test_create_portable_instance() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("portable_test");

        let instance = create_portable_instance(&root, "Skyrim SE").unwrap();
        assert_eq!(instance.game_name(), Some("Skyrim SE"));
        assert!(root.join("portable.txt").exists());
        assert!(detect_portable(&root));
    }

    #[test]
    fn test_portable_instance_info_no_ini() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("empty_dir");
        std::fs::create_dir_all(&root).unwrap();

        assert!(portable_instance_info(&root).is_err());
    }

    #[test]
    fn test_windows_separator_entries_load_without_mod_folder() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("sep_instance");
        create_instance(&root, "Skyrim SE").unwrap();

        let modlist_path = root.join("profiles/Default/modlist.txt");
        let modlist = "# This file was automatically generated by Mod Organizer.\r\n\
            +_separator_Visuals\r\n";
        std::fs::write(&modlist_path, modlist).unwrap();

        let instance = Instance::load(&root).unwrap();
        let sep = instance
            .mods
            .iter()
            .find(|m| m.name == "_separator_Visuals")
            .expect("separator entry should be synthesized from modlist");
        assert!(sep.is_separator());
        assert_eq!(sep.display_name(), "Visuals");
    }
}

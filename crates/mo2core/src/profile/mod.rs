//! Profile management.
//!
//! A profile in MO2 is a named configuration that specifies:
//! - Which mods are enabled and their priority order (modlist.txt)
//! - Plugin load order (plugins.txt, loadorder.txt)
//! - Locked plugin orders (lockedorder.txt)
//! - Profile-specific settings (settings.ini)
//! - Optionally, local saves and game settings

use std::path::{Path, PathBuf};

use crate::config::archives::ArchiveList;
use crate::config::locked::LockedOrder;
use crate::config::modlist::{ModList, ModListEntry, ModStatus};
use crate::config::plugins::{LoadOrder, PluginsTxt};
use crate::config::profile_settings::ProfileSettings;

/// A loaded MO2 profile.
#[derive(Debug, Clone)]
pub struct Profile {
    /// Profile name (directory name)
    pub name: String,
    /// Full path to the profile directory
    pub path: PathBuf,
    /// Parsed modlist.txt
    pub modlist: ModList,
    /// Parsed plugins.txt
    pub plugins: PluginsTxt,
    /// Parsed loadorder.txt
    pub load_order: LoadOrder,
    /// Parsed lockedorder.txt
    pub locked_order: LockedOrder,
    /// Profile settings (settings.ini)
    pub settings: ProfileSettings,
    /// Archives list
    pub archives: ArchiveList,
}

impl Profile {
    /// Load a profile from its directory path.
    pub fn load(path: &Path) -> anyhow::Result<Self> {
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .ok_or_else(|| anyhow::anyhow!("Invalid profile path: {:?}", path))?
            .to_string();

        let modlist = {
            let p = path.join("modlist.txt");
            if p.exists() {
                ModList::read(&p)?
            } else {
                ModList::default()
            }
        };

        let plugins = {
            let p = path.join("plugins.txt");
            if p.exists() {
                PluginsTxt::read(&p)?
            } else {
                PluginsTxt::default()
            }
        };

        let load_order = {
            let p = path.join("loadorder.txt");
            if p.exists() {
                LoadOrder::read(&p)?
            } else {
                LoadOrder::default()
            }
        };

        let locked_order = {
            let p = path.join("lockedorder.txt");
            if p.exists() {
                LockedOrder::read(&p)?
            } else {
                LockedOrder::default()
            }
        };

        let settings = {
            let p = path.join("settings.ini");
            if p.exists() {
                ProfileSettings::read(&p)?
            } else {
                ProfileSettings::new_default()
            }
        };

        let archives = {
            let p = path.join("archives.txt");
            if p.exists() {
                ArchiveList::read(&p)?
            } else {
                ArchiveList::default()
            }
        };

        Ok(Profile {
            name,
            path: path.to_path_buf(),
            modlist,
            plugins,
            load_order,
            locked_order,
            settings,
            archives,
        })
    }

    /// Save all profile files to disk.
    pub fn save(&self) -> anyhow::Result<()> {
        std::fs::create_dir_all(&self.path)?;
        self.modlist.write(&self.path.join("modlist.txt"))?;
        self.plugins.write(&self.path.join("plugins.txt"))?;
        self.load_order.write(&self.path.join("loadorder.txt"))?;
        self.locked_order
            .write(&self.path.join("lockedorder.txt"))?;
        self.settings.write(&self.path.join("settings.ini"))?;
        self.archives.write(&self.path.join("archives.txt"))?;
        Ok(())
    }

    /// Get active (enabled) mod names in priority order (lowest to highest).
    pub fn active_mods(&self) -> Vec<&ModListEntry> {
        self.modlist.enabled_sorted()
    }

    /// Check if a mod is enabled in this profile.
    pub fn is_mod_enabled(&self, name: &str) -> bool {
        self.modlist
            .find(name)
            .map(|e| e.status == ModStatus::Enabled)
            .unwrap_or(false)
    }

    /// Set a mod's enabled state.
    pub fn set_mod_enabled(&mut self, name: &str, enabled: bool) -> bool {
        self.modlist.set_enabled(name, enabled)
    }

    /// Get the priority of a mod (or None if not in the profile).
    pub fn mod_priority(&self, name: &str) -> Option<i32> {
        self.modlist.find(name).map(|e| e.priority)
    }

    /// Check if local saves are enabled.
    pub fn local_saves(&self) -> bool {
        self.settings.local_saves()
    }

    /// Check if local game settings are enabled.
    pub fn local_settings(&self) -> bool {
        self.settings.local_settings()
    }

    /// Create a new empty profile.
    pub fn create_new(profiles_dir: &Path, name: &str) -> anyhow::Result<Self> {
        let path = profiles_dir.join(name);
        if path.exists() {
            anyhow::bail!("Profile '{}' already exists", name);
        }
        std::fs::create_dir_all(&path)?;

        let profile = Profile {
            name: name.to_string(),
            path,
            modlist: ModList::default(),
            plugins: PluginsTxt::default(),
            load_order: LoadOrder::default(),
            locked_order: LockedOrder::default(),
            settings: ProfileSettings::new_default(),
            archives: ArchiveList::default(),
        };

        profile.save()?;
        Ok(profile)
    }

    /// Copy this profile to a new name.
    pub fn copy_to(&self, profiles_dir: &Path, new_name: &str) -> anyhow::Result<Profile> {
        let new_path = profiles_dir.join(new_name);
        if new_path.exists() {
            anyhow::bail!("Profile '{}' already exists", new_name);
        }

        // Copy entire directory
        copy_dir_recursive(&self.path, &new_path)?;

        // Reload from the new path
        Profile::load(&new_path)
    }
}

/// List available profile names in a profiles directory.
pub fn list_profiles(profiles_dir: &Path) -> anyhow::Result<Vec<String>> {
    let mut profiles = Vec::new();
    if !profiles_dir.exists() {
        return Ok(profiles);
    }

    for entry in std::fs::read_dir(profiles_dir)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            // A valid profile must have at least a modlist.txt
            let modlist = entry.path().join("modlist.txt");
            if modlist.exists() {
                if let Some(name) = entry.file_name().to_str() {
                    profiles.push(name.to_string());
                }
            }
        }
    }

    profiles.sort();
    Ok(profiles)
}

/// Recursively copy a directory.
fn copy_dir_recursive(src: &Path, dst: &Path) -> anyhow::Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let entry_type = entry.file_type()?;
        let dest_path = dst.join(entry.file_name());

        if entry_type.is_dir() {
            copy_dir_recursive(&entry.path(), &dest_path)?;
        } else {
            std::fs::copy(entry.path(), dest_path)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_create_and_load() {
        let tmp = tempfile::tempdir().unwrap();
        let profiles_dir = tmp.path().join("profiles");

        let profile = Profile::create_new(&profiles_dir, "TestProfile").unwrap();
        assert_eq!(profile.name, "TestProfile");
        assert!(profile.path.exists());
        assert!(profile.path.join("modlist.txt").exists());

        let loaded = Profile::load(&profile.path).unwrap();
        assert_eq!(loaded.name, "TestProfile");
    }

    #[test]
    fn test_list_profiles() {
        let tmp = tempfile::tempdir().unwrap();
        let profiles_dir = tmp.path().join("profiles");

        Profile::create_new(&profiles_dir, "Alpha").unwrap();
        Profile::create_new(&profiles_dir, "Beta").unwrap();

        let names = list_profiles(&profiles_dir).unwrap();
        assert_eq!(names, vec!["Alpha", "Beta"]);
    }

    #[test]
    fn test_copy_profile() {
        let tmp = tempfile::tempdir().unwrap();
        let profiles_dir = tmp.path().join("profiles");

        let mut original = Profile::create_new(&profiles_dir, "Original").unwrap();
        original.settings.set_local_saves(true);
        original.save().unwrap();

        let copy = original.copy_to(&profiles_dir, "Copy").unwrap();
        assert_eq!(copy.name, "Copy");
        assert!(copy.settings.local_saves());
    }

    #[test]
    fn test_mod_operations() {
        let tmp = tempfile::tempdir().unwrap();
        let profiles_dir = tmp.path().join("profiles");
        let mut profile = Profile::create_new(&profiles_dir, "Test").unwrap();

        // Manually add some mod entries
        profile.modlist.entries.push(ModListEntry {
            name: "TestMod".to_string(),
            status: ModStatus::Enabled,
            priority: 0,
        });
        profile.modlist.entries.push(ModListEntry {
            name: "DisabledMod".to_string(),
            status: ModStatus::Disabled,
            priority: 1,
        });

        assert!(profile.is_mod_enabled("TestMod"));
        assert!(!profile.is_mod_enabled("DisabledMod"));

        profile.set_mod_enabled("DisabledMod", true);
        assert!(profile.is_mod_enabled("DisabledMod"));
    }
}

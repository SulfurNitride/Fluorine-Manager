//! Global settings for MO2Linux stored at `~/.config/MO2Linux/settings.ini`.
//!
//! Tracks the last-used instance and global preferences across sessions.

use std::path::PathBuf;

use crate::config::ini::IniFile;

/// Global application settings persisted across sessions.
#[derive(Debug)]
pub struct GlobalSettings {
    ini: IniFile,
    path: PathBuf,
}

impl GlobalSettings {
    /// Load global settings from `~/.config/MO2Linux/settings.ini`.
    /// Creates the file/directory if they don't exist.
    pub fn load() -> anyhow::Result<Self> {
        let config_dir = Self::config_dir();
        let path = config_dir.join("settings.ini");

        let ini = if path.exists() {
            IniFile::read(&path)?
        } else {
            IniFile::default()
        };

        Ok(GlobalSettings { ini, path })
    }

    /// Save settings to disk.
    pub fn save(&self) -> anyhow::Result<()> {
        if let Some(parent) = self.path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        self.ini.write(&self.path)
    }

    /// Get the last-used instance path.
    pub fn last_instance(&self) -> Option<&str> {
        self.ini.get("General", "lastInstance")
    }

    /// Set the last-used instance path.
    pub fn set_last_instance(&mut self, path: &str) {
        self.ini.set("General", "lastInstance", path);
        self.add_recent_instance(path);
    }

    /// Get recently used instance paths (most recent first).
    pub fn recent_instances(&self) -> Vec<String> {
        self.ini
            .get("General", "recentInstances")
            .map(|s| {
                s.split('\n')
                    .map(str::trim)
                    .filter(|p| !p.is_empty())
                    .map(ToString::to_string)
                    .collect()
            })
            .unwrap_or_default()
    }

    /// Add an instance path to the recent list, deduplicated and capped.
    pub fn add_recent_instance(&mut self, path: &str) {
        let mut items = self.recent_instances();
        items.retain(|p| p != path);
        items.insert(0, path.to_string());
        items.truncate(24);
        self.ini
            .set("General", "recentInstances", &items.join("\n"));
    }

    /// Remove an instance path from the recent list.
    pub fn remove_recent_instance(&mut self, path: &str) {
        let mut items = self.recent_instances();
        items.retain(|p| p != path);
        if items.is_empty() {
            self.ini.remove("General", "recentInstances");
        } else {
            self.ini
                .set("General", "recentInstances", &items.join("\n"));
        }
    }

    /// Clear last instance if it points to the provided path.
    pub fn clear_last_instance_if(&mut self, path: &str) {
        if self.last_instance() == Some(path) {
            self.ini.remove("General", "lastInstance");
        }
    }

    /// Get the launch wrapper command string (e.g. "mangohud gamescope -f --").
    pub fn launch_wrapper(&self) -> Option<&str> {
        self.ini.get("General", "launchWrapper")
    }

    /// Set the launch wrapper command string.
    pub fn set_launch_wrapper(&mut self, wrapper: &str) {
        if wrapper.trim().is_empty() {
            self.ini.remove("General", "launchWrapper");
        } else {
            self.ini.set("General", "launchWrapper", wrapper.trim());
        }
    }

    /// Whether launches should use bundled UMU (`umu-run`) instead of direct `proton run`.
    ///
    /// Defaults to `true` when unset.
    pub fn use_umu_launcher(&self) -> bool {
        self.ini
            .get("General", "useUmuLauncher")
            .map(|v| v == "true" || v == "1")
            .unwrap_or(true)
    }

    /// Enable or disable UMU launcher backend.
    pub fn set_use_umu_launcher(&mut self, enabled: bool) {
        self.ini.set(
            "General",
            "useUmuLauncher",
            if enabled { "true" } else { "false" },
        );
    }

    /// Get the Nexus Mods API key.
    pub fn nexus_api_key(&self) -> Option<&str> {
        self.ini.get("Nexus", "apiKey")
    }

    /// Set the Nexus Mods API key.
    pub fn set_nexus_api_key(&mut self, key: &str) {
        if key.trim().is_empty() {
            self.ini.remove("Nexus", "apiKey");
        } else {
            self.ini.set("Nexus", "apiKey", key.trim());
        }
    }

    /// Get the global instances root directory (`~/.local/share/MO2Linux/`).
    pub fn global_instances_root() -> PathBuf {
        dirs::data_dir()
            .unwrap_or_else(|| PathBuf::from("~/.local/share"))
            .join("MO2Linux")
    }

    /// Config directory path (`~/.config/MO2Linux/`).
    fn config_dir() -> PathBuf {
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("~/.config"))
            .join("MO2Linux")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_load_default() {
        // Loading when no file exists should succeed with defaults
        let settings = GlobalSettings {
            ini: IniFile::default(),
            path: PathBuf::from("/tmp/nonexistent/settings.ini"),
        };
        assert!(settings.last_instance().is_none());
    }

    #[test]
    fn test_set_and_get_last_instance() {
        let mut settings = GlobalSettings {
            ini: IniFile::default(),
            path: PathBuf::from("/tmp/test_settings.ini"),
        };
        settings.set_last_instance("/home/user/instances/skyrim");
        assert_eq!(
            settings.last_instance(),
            Some("/home/user/instances/skyrim")
        );
        assert_eq!(
            settings.recent_instances(),
            vec!["/home/user/instances/skyrim".to_string()]
        );
    }

    #[test]
    fn test_recent_instances_dedup_order() {
        let mut settings = GlobalSettings {
            ini: IniFile::default(),
            path: PathBuf::from("/tmp/test_settings.ini"),
        };
        settings.add_recent_instance("/a");
        settings.add_recent_instance("/b");
        settings.add_recent_instance("/a");
        assert_eq!(
            settings.recent_instances(),
            vec!["/a".to_string(), "/b".to_string()]
        );
    }

    #[test]
    fn test_remove_recent_instance() {
        let mut settings = GlobalSettings {
            ini: IniFile::default(),
            path: PathBuf::from("/tmp/test_settings.ini"),
        };
        settings.add_recent_instance("/a");
        settings.add_recent_instance("/b");
        settings.remove_recent_instance("/a");
        assert_eq!(settings.recent_instances(), vec!["/b".to_string()]);
    }

    #[test]
    fn test_save_and_load() {
        let tmp = tempfile::tempdir().unwrap();
        let path = tmp.path().join("MO2Linux/settings.ini");

        let mut settings = GlobalSettings {
            ini: IniFile::default(),
            path: path.clone(),
        };
        settings.set_last_instance("/home/user/my_instance");
        settings.save().unwrap();

        // Reload
        let loaded = GlobalSettings {
            ini: IniFile::read(&path).unwrap(),
            path,
        };
        assert_eq!(loaded.last_instance(), Some("/home/user/my_instance"));
    }

    #[test]
    fn test_global_instances_root() {
        let root = GlobalSettings::global_instances_root();
        // Should end with MO2Linux
        assert!(root.ends_with("MO2Linux"));
    }
}

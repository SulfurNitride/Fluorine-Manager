//! Mod types and metadata.
//!
//! MO2 has several distinct mod types:
//! - **Regular**: User-installed mods in the `mods/` directory
//! - **Separator**: Visual separators in the mod list (name ends with `_separator`)
//! - **Overwrite**: Special folder for files written by tools/games through the VFS
//! - **Foreign**: DLC, Creation Club, or other unmanaged game content
//! - **Backup**: Backup copies of mods (name matches `.*backup[0-9]*`)

use std::path::{Path, PathBuf};

use crate::config::meta_ini::{EndorsedState, MetaIni};

/// The type of a mod entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ModType {
    Regular,
    Separator,
    Overwrite,
    Foreign,
    Backup,
}

/// Content types that can be detected in a mod's files.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ContentType {
    Plugin,     // .esp, .esm, .esl
    Texture,    // textures/
    Mesh,       // meshes/
    BsaArchive, // .bsa, .ba2
    Script,     // scripts/
    Interface,  // interface/
    Sound,      // sound/
    Music,      // music/
    Skse,       // SKSE/
    SkyProc,    // SkyProc Patchers/
    Ini,        // .ini files at root
    ModGroup,   // .modgroups
    Other,
}

/// Flags that can be set on a mod.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ModFlag {
    /// Mod has conflicts with higher-priority mods (losing files)
    ConflictLoser,
    /// Mod has conflicts with lower-priority mods (winning files)
    ConflictWinner,
    /// Mod has both winning and losing conflicts
    ConflictMixed,
    /// Mod has no valid game data detected
    NoValidGame,
    /// A newer version is available on Nexus
    UpdateAvailable,
    /// Endorsed on Nexus
    Endorsed,
    /// Has notes
    HasNotes,
    /// Has a hidden file list
    HasHiddenFiles,
}

/// Complete information about a single mod.
#[derive(Debug, Clone)]
pub struct ModInfo {
    /// Mod directory name (as it appears in mods/ folder)
    pub name: String,
    /// Full path to the mod directory
    pub path: PathBuf,
    /// Type of this mod
    pub mod_type: ModType,
    /// Whether the mod is enabled in the active profile
    pub enabled: bool,
    /// Priority (higher number = higher priority = wins conflicts)
    pub priority: i32,
    /// Parsed meta.ini (if available)
    pub meta: Option<MetaIni>,
    /// Detected content types
    pub content_types: Vec<ContentType>,
    /// Active flags
    pub flags: Vec<ModFlag>,
}

impl ModInfo {
    /// Create a new ModInfo from a mod directory path.
    pub fn from_directory(path: &Path) -> anyhow::Result<Self> {
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .ok_or_else(|| anyhow::anyhow!("Invalid mod directory name: {:?}", path))?
            .to_string();

        let mod_type = Self::detect_type(&name);

        // Try to read meta.ini
        let meta_path = path.join("meta.ini");
        let meta = if meta_path.exists() {
            MetaIni::read(&meta_path).ok()
        } else {
            None
        };

        Ok(ModInfo {
            name,
            path: path.to_path_buf(),
            mod_type,
            enabled: false,
            priority: 0,
            meta,
            content_types: Vec::new(),
            flags: Vec::new(),
        })
    }

    /// Detect mod type from directory name.
    pub fn detect_type(name: &str) -> ModType {
        let lower = name.to_lowercase();
        if lower == "overwrite" {
            ModType::Overwrite
        } else if lower.ends_with("_separator")
            || lower == "_separator"
            || lower.starts_with("_separator_")
        {
            ModType::Separator
        } else if regex::Regex::new(r".*backup\d*$").unwrap().is_match(&lower) {
            ModType::Backup
        } else {
            ModType::Regular
        }
    }

    /// Get the display name (strips `_separator` suffix for separators).
    pub fn display_name(&self) -> &str {
        if self.mod_type == ModType::Separator {
            let lower = self.name.to_lowercase();
            if lower.starts_with("_separator_") {
                return &self.name["_separator_".len()..];
            }
            if lower == "_separator" {
                return "Separator";
            }
            if let Some(idx) = lower.rfind("_separator") {
                return &self.name[..idx];
            }
        }
        &self.name
    }

    /// Check if this mod is a separator.
    pub fn is_separator(&self) -> bool {
        self.mod_type == ModType::Separator
    }

    /// Check if this mod is the overwrite folder.
    pub fn is_overwrite(&self) -> bool {
        self.mod_type == ModType::Overwrite
    }

    /// Get the Nexus mod ID if available.
    pub fn nexus_id(&self) -> Option<i64> {
        self.meta.as_ref().and_then(|m| m.mod_id())
    }

    /// Get the installed version string.
    pub fn version(&self) -> Option<&str> {
        self.meta.as_ref().and_then(|m| m.version())
    }

    /// Get the newest available version string.
    pub fn newest_version(&self) -> Option<&str> {
        self.meta.as_ref().and_then(|m| m.newest_version())
    }

    /// Check if an update is available.
    pub fn has_update(&self) -> bool {
        if let (Some(current), Some(newest)) = (self.version(), self.newest_version()) {
            !current.is_empty() && !newest.is_empty() && current != newest
        } else {
            false
        }
    }

    /// Get the endorsed state.
    pub fn endorsed(&self) -> EndorsedState {
        self.meta
            .as_ref()
            .map(|m| m.endorsed())
            .unwrap_or(EndorsedState::Unknown)
    }

    /// Get user notes.
    pub fn notes(&self) -> Option<&str> {
        self.meta.as_ref().and_then(|m| m.notes())
    }

    /// Get the installation source filename.
    pub fn installation_file(&self) -> Option<&str> {
        self.meta.as_ref().and_then(|m| m.installation_file())
    }

    /// Get the game name this mod is for.
    pub fn game_name(&self) -> Option<&str> {
        self.meta.as_ref().and_then(|m| m.game_name())
    }

    /// Scan the mod directory to detect content types.
    pub fn scan_contents(&mut self) -> anyhow::Result<()> {
        self.content_types.clear();

        if !self.path.exists() {
            return Ok(());
        }

        let mut has_plugin = false;
        let mut has_texture = false;
        let mut has_mesh = false;
        let mut has_bsa = false;
        let mut has_script = false;
        let mut has_interface = false;
        let mut has_sound = false;
        let mut has_music = false;
        let mut has_skse = false;
        let mut has_ini = false;

        for entry in walkdir::WalkDir::new(&self.path)
            .max_depth(3)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            let relative = path
                .strip_prefix(&self.path)
                .unwrap_or(path)
                .to_string_lossy()
                .to_lowercase();

            if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
                match ext.to_lowercase().as_str() {
                    "esp" | "esm" | "esl" => has_plugin = true,
                    "bsa" | "ba2" => has_bsa = true,
                    "ini" if path.parent() == Some(&self.path) => has_ini = true,
                    _ => {}
                }
            }

            if relative.starts_with("textures") {
                has_texture = true;
            }
            if relative.starts_with("meshes") {
                has_mesh = true;
            }
            if relative.starts_with("scripts") {
                has_script = true;
            }
            if relative.starts_with("interface") {
                has_interface = true;
            }
            if relative.starts_with("sound") {
                has_sound = true;
            }
            if relative.starts_with("music") {
                has_music = true;
            }
            if relative.starts_with("skse") {
                has_skse = true;
            }
        }

        if has_plugin {
            self.content_types.push(ContentType::Plugin);
        }
        if has_texture {
            self.content_types.push(ContentType::Texture);
        }
        if has_mesh {
            self.content_types.push(ContentType::Mesh);
        }
        if has_bsa {
            self.content_types.push(ContentType::BsaArchive);
        }
        if has_script {
            self.content_types.push(ContentType::Script);
        }
        if has_interface {
            self.content_types.push(ContentType::Interface);
        }
        if has_sound {
            self.content_types.push(ContentType::Sound);
        }
        if has_music {
            self.content_types.push(ContentType::Music);
        }
        if has_skse {
            self.content_types.push(ContentType::Skse);
        }
        if has_ini {
            self.content_types.push(ContentType::Ini);
        }

        Ok(())
    }

    /// Save meta.ini back to disk.
    pub fn save_meta(&self) -> anyhow::Result<()> {
        if let Some(ref meta) = self.meta {
            let meta_path = self.path.join("meta.ini");
            meta.write(&meta_path)?;
        }
        Ok(())
    }
}

/// Create a Foreign mod entry for DLC/CC content.
pub fn create_foreign(name: &str, path: &Path) -> ModInfo {
    ModInfo {
        name: name.to_string(),
        path: path.to_path_buf(),
        mod_type: ModType::Foreign,
        enabled: true,
        priority: -1, // Foreign mods have negative priority
        meta: None,
        content_types: Vec::new(),
        flags: Vec::new(),
    }
}

/// Create an Overwrite mod entry.
pub fn create_overwrite(path: &Path) -> ModInfo {
    ModInfo {
        name: "Overwrite".to_string(),
        path: path.to_path_buf(),
        mod_type: ModType::Overwrite,
        enabled: true,
        priority: i32::MAX, // Always highest priority
        meta: None,
        content_types: Vec::new(),
        flags: Vec::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_type() {
        assert_eq!(ModInfo::detect_type("My Mod"), ModType::Regular);
        assert_eq!(
            ModInfo::detect_type("UI Mods_separator"),
            ModType::Separator
        );
        assert_eq!(
            ModInfo::detect_type("_separator_Visuals"),
            ModType::Separator
        );
        assert_eq!(ModInfo::detect_type("overwrite"), ModType::Overwrite);
        assert_eq!(ModInfo::detect_type("MyMod_backup1"), ModType::Backup);
        assert_eq!(ModInfo::detect_type("MyModbackup"), ModType::Backup);
    }

    #[test]
    fn test_display_name() {
        let mut info = ModInfo {
            name: "UI Mods_separator".to_string(),
            path: PathBuf::new(),
            mod_type: ModType::Separator,
            enabled: false,
            priority: 0,
            meta: None,
            content_types: Vec::new(),
            flags: Vec::new(),
        };
        assert_eq!(info.display_name(), "UI Mods");

        info.name = "_separator_Visuals".to_string();
        assert_eq!(info.display_name(), "Visuals");

        info.name = "Regular Mod".to_string();
        info.mod_type = ModType::Regular;
        assert_eq!(info.display_name(), "Regular Mod");
    }

    #[test]
    fn test_has_update() {
        let mut info = ModInfo {
            name: "Test".to_string(),
            path: PathBuf::new(),
            mod_type: ModType::Regular,
            enabled: true,
            priority: 0,
            meta: Some(MetaIni::new_empty()),
            content_types: Vec::new(),
            flags: Vec::new(),
        };

        assert!(!info.has_update()); // Both empty

        info.meta.as_mut().unwrap().set_version("1.0");
        info.meta.as_mut().unwrap().set_newest_version("1.1");
        assert!(info.has_update());

        info.meta.as_mut().unwrap().set_newest_version("1.0");
        assert!(!info.has_update()); // Same version
    }
}

//! Parser/writer for `ModOrganizer.ini` - the main MO2 configuration file.
//!
//! Key sections:
//! - `[General]` - core settings (gameName, gameDirectory, selectedProfile, etc.)
//! - `[Settings]` - UI and behavior settings
//! - `[Widgets]` - window state
//!
//! This wraps the generic IniFile with typed accessors for known fields.

use std::path::{Path, PathBuf};

use super::ini::IniFile;
use crate::paths::normalize_any_path;

/// Parsed ModOrganizer.ini
#[derive(Debug, Clone)]
pub struct OrganizerIni {
    pub ini: IniFile,
}

impl OrganizerIni {
    fn decode_qsettings_path(raw: &str) -> PathBuf {
        let path_str = if raw.starts_with("@ByteArray(") && raw.ends_with(')') {
            &raw[11..raw.len() - 1]
        } else {
            raw
        };
        let mut normalized = normalize_any_path(path_str, None);
        while normalized.starts_with("//") {
            normalized.remove(0);
        }
        PathBuf::from(normalized)
    }

    /// Parse from string content.
    pub fn parse(content: &str) -> Self {
        OrganizerIni {
            ini: IniFile::parse(content),
        }
    }

    /// Read from file.
    pub fn read(path: &Path) -> anyhow::Result<Self> {
        let ini = IniFile::read(path)?;
        Ok(OrganizerIni { ini })
    }

    /// Write to file.
    pub fn write(&self, path: &Path) -> anyhow::Result<()> {
        self.ini.write(path)
    }

    /// Write to string.
    pub fn write_to_string(&self) -> String {
        self.ini.write_to_string()
    }

    // --- General section ---

    pub fn game_name(&self) -> Option<&str> {
        self.ini.get("General", "gameName")
    }

    pub fn set_game_name(&mut self, name: &str) {
        self.ini.set("General", "gameName", name);
    }

    /// Game directory path. May use QSettings `@ByteArray(...)` encoding or plain path.
    pub fn game_directory(&self) -> Option<PathBuf> {
        self.ini
            .get("General", "gamePath")
            .map(Self::decode_qsettings_path)
    }

    pub fn set_game_directory(&mut self, path: &Path) {
        self.ini.set(
            "General",
            "gamePath",
            &format!("@ByteArray({})", path.display()),
        );
    }

    pub fn selected_profile(&self) -> Option<&str> {
        self.ini.get("General", "selected_profile")
    }

    pub fn set_selected_profile(&mut self, profile: &str) {
        self.ini.set("General", "selected_profile", profile);
    }

    /// Base directory for mod storage. Defaults to `<instance>/mods`.
    pub fn base_directory(&self) -> Option<PathBuf> {
        self.ini
            .get("Settings", "base_directory")
            .map(Self::decode_qsettings_path)
    }

    /// Download directory. Defaults to `<instance>/downloads`.
    pub fn download_directory(&self) -> Option<PathBuf> {
        self.ini
            .get("Settings", "download_directory")
            .map(Self::decode_qsettings_path)
    }

    /// Profile directory. Defaults to `<instance>/profiles`.
    pub fn profiles_directory(&self) -> Option<PathBuf> {
        self.ini
            .get("Settings", "profiles_directory")
            .map(Self::decode_qsettings_path)
    }

    /// Overwrite directory. Defaults to `<instance>/overwrite`.
    pub fn overwrite_directory(&self) -> Option<PathBuf> {
        self.ini
            .get("Settings", "overwrite_directory")
            .map(Self::decode_qsettings_path)
    }

    /// Nexus API key.
    pub fn nexus_api_key(&self) -> Option<&str> {
        self.ini.get("Settings", "nexus_api_key")
    }

    pub fn set_nexus_api_key(&mut self, key: &str) {
        self.ini.set("Settings", "nexus_api_key", key);
    }

    // --- Wine/Proton settings ---

    /// Proton installation path (stored as `@ByteArray(...)` in `[Settings]`).
    pub fn proton_path(&self) -> Option<PathBuf> {
        self.ini
            .get("Settings", "proton_path")
            .map(Self::decode_qsettings_path)
    }

    pub fn set_proton_path(&mut self, path: &Path) {
        self.ini.set(
            "Settings",
            "proton_path",
            &format!("@ByteArray({})", path.display()),
        );
    }

    /// Wine prefix path (stored as `@ByteArray(...)` in `[Settings]`).
    pub fn wine_prefix_path(&self) -> Option<PathBuf> {
        self.ini
            .get("Settings", "wine_prefix_path")
            .map(Self::decode_qsettings_path)
    }

    pub fn set_wine_prefix_path(&mut self, path: &Path) {
        self.ini.set(
            "Settings",
            "wine_prefix_path",
            &format!("@ByteArray({})", path.display()),
        );
    }

    /// Steam App ID for the game (stored in `[General]`).
    pub fn steam_app_id(&self) -> Option<u32> {
        self.ini
            .get("General", "steamAppID")
            .and_then(|s| s.parse().ok())
    }

    pub fn set_steam_app_id(&mut self, app_id: u32) {
        self.ini.set("General", "steamAppID", &app_id.to_string());
    }

    /// Get a raw setting by section and key.
    pub fn get(&self, section: &str, key: &str) -> Option<&str> {
        self.ini.get(section, key)
    }

    /// Set a raw setting.
    pub fn set(&mut self, section: &str, key: &str, value: &str) {
        self.ini.set(section, key, value);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = "\
[General]\r\n\
gameName=Skyrim Special Edition\r\n\
selected_profile=Default\r\n\
gamePath=@ByteArray(/home/user/.steam/steam/steamapps/common/Skyrim Special Edition)\r\n\
\r\n\
[Settings]\r\n\
base_directory=@ByteArray(/home/user/MO2/mods)\r\n\
download_directory=@ByteArray(/home/user/MO2/downloads)\r\n\
nexus_api_key=abc123\r\n";

    #[test]
    fn test_parse() {
        let ini = OrganizerIni::parse(SAMPLE);
        assert_eq!(ini.game_name(), Some("Skyrim Special Edition"));
        assert_eq!(ini.selected_profile(), Some("Default"));
    }

    #[test]
    fn test_game_directory() {
        let ini = OrganizerIni::parse(SAMPLE);
        assert_eq!(
            ini.game_directory(),
            Some(PathBuf::from(
                "/home/user/.steam/steam/steamapps/common/Skyrim Special Edition"
            ))
        );
    }

    #[test]
    fn test_game_directory_backslashes_normalized() {
        let ini = OrganizerIni::parse(
            "[General]\n\
             gameName=Skyrim Special Edition\n\
             gamePath=@ByteArray(\\\\home\\\\user\\\\Games\\\\Skyrim\\\\Stock Game)\n",
        );
        assert_eq!(
            ini.game_directory(),
            Some(PathBuf::from("/home/user/Games/Skyrim/Stock Game"))
        );
    }

    #[test]
    fn test_base_directory() {
        let ini = OrganizerIni::parse(SAMPLE);
        assert_eq!(
            ini.base_directory(),
            Some(PathBuf::from("/home/user/MO2/mods"))
        );
    }

    #[test]
    fn test_nexus_api_key() {
        let ini = OrganizerIni::parse(SAMPLE);
        assert_eq!(ini.nexus_api_key(), Some("abc123"));
    }

    #[test]
    fn test_set_profile() {
        let mut ini = OrganizerIni::parse(SAMPLE);
        ini.set_selected_profile("My Profile");
        assert_eq!(ini.selected_profile(), Some("My Profile"));
    }

    #[test]
    fn test_roundtrip() {
        let ini = OrganizerIni::parse(SAMPLE);
        let output = ini.write_to_string();
        let reparsed = OrganizerIni::parse(&output);
        assert_eq!(reparsed.game_name(), Some("Skyrim Special Edition"));
        assert_eq!(reparsed.selected_profile(), Some("Default"));
    }

    #[test]
    fn test_proton_path_roundtrip() {
        let mut ini = OrganizerIni::parse(SAMPLE);
        let path = PathBuf::from("/home/user/.steam/steam/compatibilitytools.d/GE-Proton10-1");
        ini.set_proton_path(&path);
        assert_eq!(ini.proton_path(), Some(path));
    }

    #[test]
    fn test_wine_prefix_path_roundtrip() {
        let mut ini = OrganizerIni::parse(SAMPLE);
        let path = PathBuf::from("/home/user/.steam/steam/steamapps/compatdata/12345/pfx");
        ini.set_wine_prefix_path(&path);
        assert_eq!(ini.wine_prefix_path(), Some(path));
    }

    #[test]
    fn test_steam_app_id_roundtrip() {
        let mut ini = OrganizerIni::parse(SAMPLE);
        assert_eq!(ini.steam_app_id(), None);
        ini.set_steam_app_id(489830);
        assert_eq!(ini.steam_app_id(), Some(489830));
    }
}

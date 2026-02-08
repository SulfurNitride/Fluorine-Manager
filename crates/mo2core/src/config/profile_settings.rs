//! Parser/writer for per-profile `settings.ini`.
//!
//! Located at `profiles/<name>/settings.ini`.
//! Uses QSettings INI format.
//!
//! Standard keys:
//! - `LocalSaves` (bool) - use profile-local saves instead of game folder
//! - `LocalSettings` (bool) - use profile-local game INI settings
//! - `AutomaticArchiveInvalidation` (bool) - auto-invalidate BSA archives

use std::path::Path;

use super::ini::IniFile;

/// Profile-specific settings.
#[derive(Debug, Clone)]
pub struct ProfileSettings {
    pub ini: IniFile,
}

impl ProfileSettings {
    /// Parse from string content.
    pub fn parse(content: &str) -> Self {
        ProfileSettings {
            ini: IniFile::parse(content),
        }
    }

    /// Read from file.
    pub fn read(path: &Path) -> anyhow::Result<Self> {
        let ini = IniFile::read(path)?;
        Ok(ProfileSettings { ini })
    }

    /// Write to file.
    pub fn write(&self, path: &Path) -> anyhow::Result<()> {
        self.ini.write(path)
    }

    /// Write to string.
    pub fn write_to_string(&self) -> String {
        self.ini.write_to_string()
    }

    /// Create default profile settings.
    pub fn new_default() -> Self {
        let mut ini = IniFile::default();
        ini.set("General", "LocalSaves", "false");
        ini.set("General", "LocalSettings", "true");
        ini.set("General", "AutomaticArchiveInvalidation", "true");
        ProfileSettings { ini }
    }

    fn get_bool(&self, key: &str) -> bool {
        self.ini
            .get("General", key)
            .map(|s| s == "true" || s == "1")
            .unwrap_or(false)
    }

    fn set_bool(&mut self, key: &str, value: bool) {
        self.ini
            .set("General", key, if value { "true" } else { "false" });
    }

    pub fn local_saves(&self) -> bool {
        self.get_bool("LocalSaves")
    }

    pub fn set_local_saves(&mut self, enabled: bool) {
        self.set_bool("LocalSaves", enabled);
    }

    pub fn local_settings(&self) -> bool {
        self.get_bool("LocalSettings")
    }

    pub fn set_local_settings(&mut self, enabled: bool) {
        self.set_bool("LocalSettings", enabled);
    }

    pub fn automatic_archive_invalidation(&self) -> bool {
        self.get_bool("AutomaticArchiveInvalidation")
    }

    pub fn set_automatic_archive_invalidation(&mut self, enabled: bool) {
        self.set_bool("AutomaticArchiveInvalidation", enabled);
    }

    /// Get an arbitrary setting.
    pub fn get(&self, section: &str, key: &str) -> Option<&str> {
        self.ini.get(section, key)
    }

    /// Set an arbitrary setting.
    pub fn set(&mut self, section: &str, key: &str, value: &str) {
        self.ini.set(section, key, value);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_defaults() {
        let settings = ProfileSettings::new_default();
        assert!(!settings.local_saves());
        assert!(settings.local_settings());
        assert!(settings.automatic_archive_invalidation());
    }

    #[test]
    fn test_parse() {
        let content = "[General]\r\nLocalSaves=true\r\nLocalSettings=false\r\nAutomaticArchiveInvalidation=true\r\n";
        let settings = ProfileSettings::parse(content);
        assert!(settings.local_saves());
        assert!(!settings.local_settings());
        assert!(settings.automatic_archive_invalidation());
    }

    #[test]
    fn test_set() {
        let mut settings = ProfileSettings::new_default();
        settings.set_local_saves(true);
        assert!(settings.local_saves());

        settings.set_automatic_archive_invalidation(false);
        assert!(!settings.automatic_archive_invalidation());
    }

    #[test]
    fn test_roundtrip() {
        let mut settings = ProfileSettings::new_default();
        settings.set_local_saves(true);
        let output = settings.write_to_string();
        let reparsed = ProfileSettings::parse(&output);
        assert!(reparsed.local_saves());
        assert!(reparsed.local_settings());
    }
}

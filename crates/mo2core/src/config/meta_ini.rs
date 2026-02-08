//! Parser/writer for per-mod `meta.ini` files.
//!
//! Each mod directory contains a `meta.ini` in QSettings INI format with sections:
//! - `[General]` - mod metadata (modid, version, gameName, etc.)
//! - `[installedFiles]` - files that were installed from archives
//! - `[Plugins/*]` - per-plugin settings within the mod
//!
//! Key fields in `[General]`:
//! - `modid` - Nexus mod ID
//! - `version` - installed version string
//! - `newestVersion` - latest known version on Nexus
//! - `ignoredVersion` - version to suppress update notifications
//! - `category` - comma-separated category IDs (e.g., "54,42,")
//! - `nexusCategory` - Nexus category ID
//! - `installationFile` - original archive filename
//! - `gameName` - game this mod is for
//! - `endorsed` - 0=not endorsed, 1=endorsed, 2=won't endorse
//! - `tracked` - whether tracking on Nexus
//! - `comments` / `notes` - user text
//! - `url` - Nexus mod page URL
//! - `hasCustomURL` - whether URL was manually set

use std::path::Path;

use super::ini::IniFile;

/// Endorsement state for a mod on Nexus.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EndorsedState {
    /// Not endorsed (value: "")
    Unknown,
    /// Endorsed (value: "true" or "Endorsed")
    Endorsed,
    /// Chose not to endorse (value: "Abstained")
    Abstained,
}

impl EndorsedState {
    pub fn from_str(s: &str) -> Self {
        match s.trim().to_lowercase().as_str() {
            "true" | "endorsed" => Self::Endorsed,
            "abstained" | "false" => Self::Abstained,
            _ => Self::Unknown,
        }
    }

    pub fn to_ini_value(&self) -> &'static str {
        match self {
            Self::Unknown => "",
            Self::Endorsed => "Endorsed",
            Self::Abstained => "Abstained",
        }
    }
}

/// Parsed and queryable meta.ini for a single mod.
#[derive(Debug, Clone)]
pub struct MetaIni {
    /// The underlying INI file for full access.
    pub ini: IniFile,
}

impl MetaIni {
    /// Parse from string content.
    pub fn parse(content: &str) -> Self {
        MetaIni {
            ini: IniFile::parse(content),
        }
    }

    /// Read from a file path.
    pub fn read(path: &Path) -> anyhow::Result<Self> {
        let ini = IniFile::read(path)?;
        Ok(MetaIni { ini })
    }

    /// Write to a file path.
    pub fn write(&self, path: &Path) -> anyhow::Result<()> {
        self.ini.write(path)
    }

    /// Write to string.
    pub fn write_to_string(&self) -> String {
        self.ini.write_to_string()
    }

    // --- General section accessors ---

    pub fn mod_id(&self) -> Option<i64> {
        self.ini
            .get("General", "modid")
            .and_then(|s| s.parse().ok())
    }

    pub fn set_mod_id(&mut self, id: i64) {
        self.ini.set("General", "modid", &id.to_string());
    }

    pub fn version(&self) -> Option<&str> {
        self.ini.get("General", "version")
    }

    pub fn set_version(&mut self, version: &str) {
        self.ini.set("General", "version", version);
    }

    pub fn newest_version(&self) -> Option<&str> {
        self.ini.get("General", "newestVersion")
    }

    pub fn set_newest_version(&mut self, version: &str) {
        self.ini.set("General", "newestVersion", version);
    }

    pub fn ignored_version(&self) -> Option<&str> {
        self.ini.get("General", "ignoredVersion")
    }

    pub fn set_ignored_version(&mut self, version: &str) {
        self.ini.set("General", "ignoredVersion", version);
    }

    pub fn game_name(&self) -> Option<&str> {
        self.ini.get("General", "gameName")
    }

    pub fn set_game_name(&mut self, name: &str) {
        self.ini.set("General", "gameName", name);
    }

    pub fn category_ids(&self) -> Vec<i32> {
        self.ini
            .get("General", "category")
            .map(|s| s.split(',').filter_map(|c| c.trim().parse().ok()).collect())
            .unwrap_or_default()
    }

    pub fn set_category_ids(&mut self, ids: &[i32]) {
        let s: String = ids
            .iter()
            .map(|id| id.to_string())
            .collect::<Vec<_>>()
            .join(",")
            + ",";
        self.ini.set("General", "category", &s);
    }

    pub fn nexus_category(&self) -> Option<i32> {
        self.ini
            .get("General", "nexusCategory")
            .and_then(|s| s.parse().ok())
    }

    pub fn installation_file(&self) -> Option<&str> {
        self.ini.get("General", "installationFile")
    }

    pub fn set_installation_file(&mut self, filename: &str) {
        self.ini.set("General", "installationFile", filename);
    }

    pub fn endorsed(&self) -> EndorsedState {
        self.ini
            .get("General", "endorsed")
            .map(EndorsedState::from_str)
            .unwrap_or(EndorsedState::Unknown)
    }

    pub fn set_endorsed(&mut self, state: EndorsedState) {
        self.ini.set("General", "endorsed", state.to_ini_value());
    }

    pub fn tracked(&self) -> bool {
        self.ini
            .get("General", "tracked")
            .map(|s| s == "true" || s == "1")
            .unwrap_or(false)
    }

    pub fn url(&self) -> Option<&str> {
        self.ini.get("General", "url")
    }

    pub fn set_url(&mut self, url: &str) {
        self.ini.set("General", "url", url);
    }

    pub fn comments(&self) -> Option<&str> {
        self.ini.get("General", "comments")
    }

    pub fn notes(&self) -> Option<&str> {
        self.ini.get("General", "notes")
    }

    pub fn set_notes(&mut self, notes: &str) {
        self.ini.set("General", "notes", notes);
    }

    pub fn file_id(&self) -> Option<i64> {
        self.ini
            .get("General", "fileID")
            .and_then(|s| s.parse().ok())
    }

    /// Get installed files list from [installedFiles] section.
    pub fn installed_files(&self) -> Vec<String> {
        let map = self.ini.section_map("installedFiles");
        // QSettings stores arrays as: size=N, 1\name=..., 2\name=...
        let count: usize = map.get("size").and_then(|s| s.parse().ok()).unwrap_or(0);

        let mut files = Vec::with_capacity(count);
        for i in 1..=count {
            let key = format!("{}\\name", i);
            if let Some(name) = map.get(&key) {
                files.push(name.clone());
            }
        }
        files
    }

    /// Create a new empty meta.ini with defaults.
    pub fn new_empty() -> Self {
        let mut ini = IniFile::default();
        ini.set("General", "modid", "0");
        ini.set("General", "version", "");
        ini.set("General", "newestVersion", "");
        ini.set("General", "category", "");
        ini.set("General", "installationFile", "");
        MetaIni { ini }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE_META: &str = "\
[General]\r\n\
modid=1234\r\n\
version=1.0.0\r\n\
newestVersion=1.1.0\r\n\
category=54,42,\r\n\
nexusCategory=5\r\n\
installationFile=MyMod-1234-1-0-0.7z\r\n\
gameName=Skyrim Special Edition\r\n\
endorsed=Endorsed\r\n\
tracked=true\r\n\
url=https://www.nexusmods.com/skyrimspecialedition/mods/1234\r\n\
notes=Great mod\r\n\
\r\n\
[installedFiles]\r\n\
size=2\r\n\
1\\name=MyMod-1234-1-0-0.7z\r\n\
2\\name=MyMod-Optional-1234-1-0-0.7z\r\n";

    #[test]
    fn test_parse_general() {
        let meta = MetaIni::parse(SAMPLE_META);
        assert_eq!(meta.mod_id(), Some(1234));
        assert_eq!(meta.version(), Some("1.0.0"));
        assert_eq!(meta.newest_version(), Some("1.1.0"));
        assert_eq!(meta.game_name(), Some("Skyrim Special Edition"));
        assert_eq!(meta.endorsed(), EndorsedState::Endorsed);
        assert!(meta.tracked());
        assert_eq!(meta.notes(), Some("Great mod"));
    }

    #[test]
    fn test_category_ids() {
        let meta = MetaIni::parse(SAMPLE_META);
        assert_eq!(meta.category_ids(), vec![54, 42]);
    }

    #[test]
    fn test_installed_files() {
        let meta = MetaIni::parse(SAMPLE_META);
        let files = meta.installed_files();
        assert_eq!(files.len(), 2);
        assert_eq!(files[0], "MyMod-1234-1-0-0.7z");
        assert_eq!(files[1], "MyMod-Optional-1234-1-0-0.7z");
    }

    #[test]
    fn test_set_values() {
        let mut meta = MetaIni::new_empty();
        meta.set_mod_id(5678);
        meta.set_version("2.0.0");
        meta.set_game_name("Fallout 4");
        meta.set_endorsed(EndorsedState::Abstained);

        assert_eq!(meta.mod_id(), Some(5678));
        assert_eq!(meta.version(), Some("2.0.0"));
        assert_eq!(meta.game_name(), Some("Fallout 4"));
        assert_eq!(meta.endorsed(), EndorsedState::Abstained);
    }

    #[test]
    fn test_roundtrip() {
        let meta = MetaIni::parse(SAMPLE_META);
        let output = meta.write_to_string();
        let reparsed = MetaIni::parse(&output);
        assert_eq!(reparsed.mod_id(), Some(1234));
        assert_eq!(reparsed.version(), Some("1.0.0"));
        assert_eq!(reparsed.game_name(), Some("Skyrim Special Edition"));
    }

    #[test]
    fn test_endorsed_states() {
        assert_eq!(EndorsedState::from_str("Endorsed"), EndorsedState::Endorsed);
        assert_eq!(EndorsedState::from_str("true"), EndorsedState::Endorsed);
        assert_eq!(
            EndorsedState::from_str("Abstained"),
            EndorsedState::Abstained
        );
        assert_eq!(EndorsedState::from_str(""), EndorsedState::Unknown);
    }
}

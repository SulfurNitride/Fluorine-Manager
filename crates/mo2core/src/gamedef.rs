//! Game definitions — provides game-specific paths and metadata.
//!
//! This replaces MO2's IPluginGame/BasicGame plugin system for path information.
//! Each supported game has a static definition with binary name, data directory name,
//! INI files, Steam/GOG IDs, etc. The actual game directory is set at runtime from
//! the instance config (gamePath) or auto-detected via NaK.

use std::path::{Path, PathBuf};

/// Identifies a specific supported game.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum GameId {
    SkyrimSE,
    SkyrimLE,
    SkyrimVR,
    Fallout4,
    Fallout4VR,
    FalloutNV,
    Fallout3,
    Oblivion,
    Morrowind,
    Starfield,
    EnderalSE,
    EnderalLE,
    Fallout76,
}

/// Static metadata for a game (known at compile time).
struct GameMeta {
    id: GameId,
    name: &'static str,
    short_name: &'static str,
    nexus_name: &'static str,
    binary_name: &'static str,
    /// Name of the data directory relative to game root.
    /// "Data" for most games, "Data Files" for Morrowind.
    data_dir_name: &'static str,
    ini_files: &'static [&'static str],
    steam_app_id: Option<u32>,
    gog_app_id: Option<u32>,
    /// Folder name under Documents/My Games/ (if applicable).
    my_games_folder: Option<&'static str>,
}

/// Complete game definition with runtime paths resolved.
#[derive(Debug, Clone)]
pub struct GameDef {
    pub id: GameId,
    pub name: String,
    pub short_name: String,
    pub nexus_name: String,
    pub binary_name: String,
    pub data_dir_name: String,
    pub ini_files: Vec<String>,
    pub steam_app_id: Option<u32>,
    pub gog_app_id: Option<u32>,
    pub my_games_folder: Option<String>,
    /// Root game directory (where the .exe lives).
    pub game_directory: PathBuf,
}

impl GameDef {
    /// Path to the game's data directory (game_directory / data_dir_name).
    pub fn data_directory(&self) -> PathBuf {
        self.game_directory.join(&self.data_dir_name)
    }

    /// Path to the game's documents directory (~/Documents/My Games/<folder>).
    /// Returns None if the game doesn't use a My Games folder.
    pub fn documents_directory(&self) -> Option<PathBuf> {
        self.my_games_folder.as_ref().map(|folder| {
            let docs = dirs::document_dir().unwrap_or_else(|| {
                dirs::home_dir()
                    .unwrap_or_else(|| PathBuf::from("/home"))
                    .join("Documents")
            });
            docs.join("My Games").join(folder)
        })
    }

    /// Full path to the game binary.
    pub fn binary_path(&self) -> PathBuf {
        self.game_directory.join(&self.binary_name)
    }

    /// Primary plugins that MO2 treats as fixed-leading in load order.
    ///
    /// This mirrors upstream MO2 game plugins' `primaryPlugins()` behavior.
    pub fn primary_plugins(&self) -> Vec<String> {
        let mut plugins: Vec<String> = match self.id {
            GameId::SkyrimSE => vec![
                "skyrim.esm".to_string(),
                "update.esm".to_string(),
                "dawnguard.esm".to_string(),
                "hearthfires.esm".to_string(),
                "dragonborn.esm".to_string(),
            ],
            GameId::SkyrimLE => vec!["skyrim.esm".to_string(), "update.esm".to_string()],
            GameId::SkyrimVR => vec![
                "skyrim.esm".to_string(),
                "update.esm".to_string(),
                "dawnguard.esm".to_string(),
                "hearthfires.esm".to_string(),
                "dragonborn.esm".to_string(),
                "skyrimvr.esm".to_string(),
            ],
            GameId::Fallout4 => vec![
                "fallout4.esm".to_string(),
                "dlcrobot.esm".to_string(),
                "dlcworkshop01.esm".to_string(),
                "dlccoast.esm".to_string(),
                "dlcworkshop02.esm".to_string(),
                "dlcworkshop03.esm".to_string(),
                "dlcnukaworld.esm".to_string(),
                "dlcultrahighresolution.esm".to_string(),
            ],
            GameId::Fallout4VR => {
                vec!["fallout4.esm".to_string(), "fallout4_vr.esm".to_string()]
            }
            GameId::FalloutNV => vec!["falloutnv.esm".to_string()],
            GameId::Fallout3 => vec!["fallout3.esm".to_string()],
            GameId::Oblivion => vec!["oblivion.esm".to_string(), "update.esm".to_string()],
            GameId::Starfield => vec![
                "Starfield.esm".to_string(),
                "Constellation.esm".to_string(),
                "ShatteredSpace.esm".to_string(),
                "OldMars.esm".to_string(),
                "SFBGS003.esm".to_string(),
                "SFBGS004.esm".to_string(),
                "SFBGS006.esm".to_string(),
                "SFBGS007.esm".to_string(),
                "SFBGS008.esm".to_string(),
                "BlueprintShips-Starfield.esm".to_string(),
            ],
            GameId::EnderalSE => vec![
                "skyrim.esm".to_string(),
                "update.esm".to_string(),
                "dawnguard.esm".to_string(),
                "hearthfires.esm".to_string(),
                "dragonborn.esm".to_string(),
                "enderal - forgotten stories.esm".to_string(),
                "skyui_se.esp".to_string(),
            ],
            GameId::EnderalLE => vec![
                "Skyrim.esm".to_string(),
                "Enderal - Forgotten Stories.esm".to_string(),
                "Update.esm".to_string(),
            ],
            GameId::Morrowind | GameId::Fallout76 => Vec::new(),
        };

        match self.id {
            GameId::SkyrimSE | GameId::SkyrimVR => {
                plugins.extend(parse_ccc_file(&self.game_directory.join("Skyrim.ccc")));
            }
            GameId::Fallout4 | GameId::Fallout4VR => {
                plugins.extend(parse_ccc_file(&self.game_directory.join("Fallout4.ccc")));
            }
            GameId::Starfield => {
                // Upstream checks My Games/Starfield.ccc first, then game dir fallback.
                let docs_ccc = self
                    .documents_directory()
                    .map(|p| p.join("Starfield.ccc"))
                    .filter(|p| p.exists());
                if let Some(path) = docs_ccc {
                    plugins.extend(parse_ccc_file(&path));
                } else {
                    plugins.extend(parse_ccc_file(&self.game_directory.join("Starfield.ccc")));
                }
            }
            _ => {}
        }

        dedupe_case_insensitive(plugins)
    }

    /// Create a GameDef from an MO2 instance config.
    ///
    /// Reads `gameName` and `gamePath` from ModOrganizer.ini.
    /// Returns None if the game name isn't recognized or no game path is set.
    pub fn from_instance(game_name: Option<&str>, game_directory: Option<&Path>) -> Option<Self> {
        let name = game_name?;
        let meta = find_meta_by_name(name)?;
        let dir = game_directory?.to_path_buf();

        Some(Self::from_meta(meta, dir))
    }

    /// Create a GameDef from a GameId with a known game directory.
    pub fn from_id(id: GameId, game_directory: PathBuf) -> Option<Self> {
        let meta = find_meta_by_id(id)?;
        Some(Self::from_meta(meta, game_directory))
    }

    /// Try to auto-detect a game using NaK and create a GameDef.
    pub fn detect(game_name: &str) -> Option<Self> {
        let meta = find_meta_by_name(game_name)?;

        // Try Steam detection via NaK
        if let Some(steam_id) = meta.steam_app_id {
            if let Some(path) = nak_rust::game_finder::find_game_install_path(&steam_id.to_string())
            {
                return Some(Self::from_meta(meta, path));
            }
        }

        None
    }

    /// Resolve a GameDef for an instance, trying config first, then auto-detection.
    pub fn resolve(game_name: Option<&str>, game_directory: Option<&Path>) -> Option<Self> {
        // If we have both name and path from config, use them directly
        if let Some(def) = Self::from_instance(game_name, game_directory) {
            return Some(def);
        }

        // If we only have a name, try auto-detection
        if let Some(name) = game_name {
            return Self::detect(name);
        }

        None
    }

    fn from_meta(meta: &GameMeta, game_directory: PathBuf) -> Self {
        GameDef {
            id: meta.id,
            name: meta.name.to_string(),
            short_name: meta.short_name.to_string(),
            nexus_name: meta.nexus_name.to_string(),
            binary_name: meta.binary_name.to_string(),
            data_dir_name: meta.data_dir_name.to_string(),
            ini_files: meta.ini_files.iter().map(|s| s.to_string()).collect(),
            steam_app_id: meta.steam_app_id,
            gog_app_id: meta.gog_app_id,
            my_games_folder: meta.my_games_folder.map(|s| s.to_string()),
            game_directory,
        }
    }
}

fn parse_ccc_file(path: &Path) -> Vec<String> {
    let Ok(content) = std::fs::read_to_string(path) else {
        return Vec::new();
    };

    let mut out = Vec::new();
    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }
        out.push(trimmed.to_string());
    }
    dedupe_case_insensitive(out)
}

fn dedupe_case_insensitive(items: Vec<String>) -> Vec<String> {
    let mut seen = std::collections::HashSet::new();
    let mut out = Vec::new();
    for item in items {
        let lower = item.to_lowercase();
        if seen.insert(lower) {
            out.push(item);
        }
    }
    out
}

// ── Known game database ──────────────────────────────────────────────

const KNOWN_GAMES: &[GameMeta] = &[
    GameMeta {
        id: GameId::SkyrimSE,
        name: "Skyrim Special Edition",
        short_name: "SkyrimSE",
        nexus_name: "skyrimspecialedition",
        binary_name: "SkyrimSE.exe",
        data_dir_name: "Data",
        ini_files: &["Skyrim.ini", "SkyrimPrefs.ini", "SkyrimCustom.ini"],
        steam_app_id: Some(489830),
        gog_app_id: None,
        my_games_folder: Some("Skyrim Special Edition"),
    },
    GameMeta {
        id: GameId::SkyrimLE,
        name: "Skyrim",
        short_name: "Skyrim",
        nexus_name: "skyrim",
        binary_name: "TESV.exe",
        data_dir_name: "Data",
        ini_files: &["Skyrim.ini", "SkyrimPrefs.ini"],
        steam_app_id: Some(72850),
        gog_app_id: None,
        my_games_folder: Some("Skyrim"),
    },
    GameMeta {
        id: GameId::SkyrimVR,
        name: "Skyrim VR",
        short_name: "SkyrimVR",
        nexus_name: "skyrimspecialedition",
        binary_name: "SkyrimVR.exe",
        data_dir_name: "Data",
        ini_files: &["Skyrim.ini", "SkyrimPrefs.ini", "SkyrimVR.ini"],
        steam_app_id: Some(611670),
        gog_app_id: None,
        my_games_folder: Some("Skyrim VR"),
    },
    GameMeta {
        id: GameId::Fallout4,
        name: "Fallout 4",
        short_name: "Fallout4",
        nexus_name: "fallout4",
        binary_name: "Fallout4.exe",
        data_dir_name: "Data",
        ini_files: &["Fallout4.ini", "Fallout4Prefs.ini", "Fallout4Custom.ini"],
        steam_app_id: Some(377160),
        gog_app_id: None,
        my_games_folder: Some("Fallout4"),
    },
    GameMeta {
        id: GameId::Fallout4VR,
        name: "Fallout 4 VR",
        short_name: "Fallout4VR",
        nexus_name: "fallout4",
        binary_name: "Fallout4VR.exe",
        data_dir_name: "Data",
        ini_files: &["Fallout4.ini", "Fallout4Prefs.ini", "Fallout4VRCustom.ini"],
        steam_app_id: Some(611660),
        gog_app_id: None,
        my_games_folder: Some("Fallout4VR"),
    },
    GameMeta {
        id: GameId::FalloutNV,
        name: "New Vegas",
        short_name: "FalloutNV",
        nexus_name: "newvegas",
        binary_name: "FalloutNV.exe",
        data_dir_name: "Data",
        ini_files: &["Fallout.ini", "FalloutPrefs.ini", "FalloutCustom.ini"],
        steam_app_id: Some(22380),
        gog_app_id: Some(1454587428),
        my_games_folder: Some("FalloutNV"),
    },
    GameMeta {
        id: GameId::Fallout3,
        name: "Fallout 3",
        short_name: "Fallout3",
        nexus_name: "fallout3",
        binary_name: "Fallout3.exe",
        data_dir_name: "Data",
        ini_files: &["Fallout.ini", "FalloutPrefs.ini"],
        steam_app_id: Some(22300),
        gog_app_id: Some(1454315831),
        my_games_folder: Some("Fallout3"),
    },
    GameMeta {
        id: GameId::Oblivion,
        name: "Oblivion",
        short_name: "Oblivion",
        nexus_name: "oblivion",
        binary_name: "Oblivion.exe",
        data_dir_name: "Data",
        ini_files: &["Oblivion.ini"],
        steam_app_id: Some(22330),
        gog_app_id: Some(1458058109),
        my_games_folder: Some("Oblivion"),
    },
    GameMeta {
        id: GameId::Morrowind,
        name: "Morrowind",
        short_name: "Morrowind",
        nexus_name: "morrowind",
        binary_name: "Morrowind.exe",
        data_dir_name: "Data Files",
        ini_files: &["Morrowind.ini"],
        steam_app_id: Some(22320),
        gog_app_id: Some(1435828767),
        my_games_folder: None, // Morrowind uses install dir for INI
    },
    GameMeta {
        id: GameId::Starfield,
        name: "Starfield",
        short_name: "Starfield",
        nexus_name: "starfield",
        binary_name: "Starfield.exe",
        data_dir_name: "Data",
        ini_files: &["StarfieldPrefs.ini", "StarfieldCustom.ini"],
        steam_app_id: Some(1716740),
        gog_app_id: None,
        my_games_folder: Some("Starfield"),
    },
    GameMeta {
        id: GameId::EnderalSE,
        name: "Enderal Special Edition",
        short_name: "EnderalSE",
        nexus_name: "enderalspecialedition",
        binary_name: "SkyrimSE.exe",
        data_dir_name: "Data",
        ini_files: &["Enderal.ini", "EnderalPrefs.ini"],
        steam_app_id: Some(976620),
        gog_app_id: None,
        my_games_folder: Some("Enderal Special Edition"),
    },
    GameMeta {
        id: GameId::EnderalLE,
        name: "Enderal",
        short_name: "Enderal",
        nexus_name: "enderal",
        binary_name: "TESV.exe",
        data_dir_name: "Data",
        ini_files: &["Enderal.ini", "EnderalPrefs.ini"],
        steam_app_id: Some(933480),
        gog_app_id: None,
        my_games_folder: Some("Enderal"),
    },
    GameMeta {
        id: GameId::Fallout76,
        name: "Fallout 76",
        short_name: "Fallout76",
        nexus_name: "fallout76",
        binary_name: "Fallout76.exe",
        data_dir_name: "Data",
        ini_files: &["Fallout76.ini", "Fallout76Prefs.ini", "Fallout76Custom.ini"],
        steam_app_id: Some(1151340),
        gog_app_id: None,
        my_games_folder: Some("Fallout 76"),
    },
];

fn find_meta_by_id(id: GameId) -> Option<&'static GameMeta> {
    KNOWN_GAMES.iter().find(|g| g.id == id)
}

fn find_meta_by_name(name: &str) -> Option<&'static GameMeta> {
    let lower = name.to_lowercase();
    KNOWN_GAMES.iter().find(|g| g.name.to_lowercase() == lower)
}

/// List all known game names (for UI dropdowns, etc.).
pub fn known_game_names() -> Vec<&'static str> {
    KNOWN_GAMES.iter().map(|g| g.name).collect()
}

/// Get a game's short name from its display name.
pub fn short_name_for(game_name: &str) -> Option<&'static str> {
    find_meta_by_name(game_name).map(|m| m.short_name)
}

/// Get the Steam App ID for a game by name.
pub fn steam_app_id_for(game_name: &str) -> Option<u32> {
    find_meta_by_name(game_name).and_then(|m| m.steam_app_id)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_known_game_names() {
        let names = known_game_names();
        assert!(names.contains(&"Skyrim Special Edition"));
        assert!(names.contains(&"Fallout 4"));
        assert!(names.contains(&"Morrowind"));
        assert!(names.contains(&"Starfield"));
        assert_eq!(names.len(), 13);
    }

    #[test]
    fn test_find_by_name_case_insensitive() {
        assert!(find_meta_by_name("skyrim special edition").is_some());
        assert!(find_meta_by_name("SKYRIM SPECIAL EDITION").is_some());
        assert!(find_meta_by_name("Skyrim Special Edition").is_some());
        assert!(find_meta_by_name("nonexistent game").is_none());
    }

    #[test]
    fn test_gamedef_from_id() {
        let def = GameDef::from_id(GameId::SkyrimSE, PathBuf::from("/games/skyrimse")).unwrap();
        assert_eq!(def.name, "Skyrim Special Edition");
        assert_eq!(def.short_name, "SkyrimSE");
        assert_eq!(def.binary_name, "SkyrimSE.exe");
        assert_eq!(def.data_dir_name, "Data");
        assert_eq!(def.steam_app_id, Some(489830));
        assert_eq!(def.game_directory, PathBuf::from("/games/skyrimse"));
    }

    #[test]
    fn test_data_directory() {
        let def = GameDef::from_id(GameId::SkyrimSE, PathBuf::from("/games/skyrimse")).unwrap();
        assert_eq!(def.data_directory(), PathBuf::from("/games/skyrimse/Data"));

        let def = GameDef::from_id(GameId::Morrowind, PathBuf::from("/games/morrowind")).unwrap();
        assert_eq!(
            def.data_directory(),
            PathBuf::from("/games/morrowind/Data Files")
        );
    }

    #[test]
    fn test_binary_path() {
        let def = GameDef::from_id(GameId::Fallout4, PathBuf::from("/games/fo4")).unwrap();
        assert_eq!(def.binary_path(), PathBuf::from("/games/fo4/Fallout4.exe"));
    }

    #[test]
    fn test_documents_directory() {
        let def = GameDef::from_id(GameId::SkyrimSE, PathBuf::from("/games/skyrimse")).unwrap();
        let docs = def.documents_directory();
        assert!(docs.is_some());
        let docs = docs.unwrap();
        assert!(docs.to_string_lossy().contains("My Games"));
        assert!(docs.to_string_lossy().contains("Skyrim Special Edition"));

        // Morrowind has no My Games folder
        let def = GameDef::from_id(GameId::Morrowind, PathBuf::from("/games/morrowind")).unwrap();
        assert!(def.documents_directory().is_none());
    }

    #[test]
    fn test_from_instance() {
        let def = GameDef::from_instance(
            Some("Skyrim Special Edition"),
            Some(Path::new("/games/skyrimse")),
        );
        assert!(def.is_some());
        let def = def.unwrap();
        assert_eq!(def.id, GameId::SkyrimSE);
        assert_eq!(def.game_directory, PathBuf::from("/games/skyrimse"));
    }

    #[test]
    fn test_from_instance_unknown_game() {
        let def = GameDef::from_instance(Some("Unknown Game"), Some(Path::new("/games/unknown")));
        assert!(def.is_none());
    }

    #[test]
    fn test_from_instance_no_path() {
        let def = GameDef::from_instance(Some("Skyrim Special Edition"), None);
        assert!(def.is_none());
    }

    #[test]
    fn test_ini_files() {
        let def = GameDef::from_id(GameId::SkyrimSE, PathBuf::from("/games/skyrimse")).unwrap();
        assert_eq!(
            def.ini_files,
            vec!["Skyrim.ini", "SkyrimPrefs.ini", "SkyrimCustom.ini"]
        );
    }

    #[test]
    fn test_short_name_for() {
        assert_eq!(short_name_for("Skyrim Special Edition"), Some("SkyrimSE"));
        assert_eq!(short_name_for("Morrowind"), Some("Morrowind"));
        assert_eq!(short_name_for("nonexistent"), None);
    }

    #[test]
    fn test_steam_app_id_for() {
        assert_eq!(steam_app_id_for("Skyrim Special Edition"), Some(489830));
        assert_eq!(steam_app_id_for("Fallout 4"), Some(377160));
        assert_eq!(steam_app_id_for("nonexistent"), None);
    }

    #[test]
    fn test_primary_plugins_skyrimse_starts_with_core_set() {
        let def = GameDef::from_id(GameId::SkyrimSE, PathBuf::from("/games/skyrimse")).unwrap();
        let primary = def.primary_plugins();
        assert!(primary.len() >= 5);
        assert_eq!(primary[0].to_lowercase(), "skyrim.esm");
        assert_eq!(primary[1].to_lowercase(), "update.esm");
        assert_eq!(primary[2].to_lowercase(), "dawnguard.esm");
        assert_eq!(primary[3].to_lowercase(), "hearthfires.esm");
        assert_eq!(primary[4].to_lowercase(), "dragonborn.esm");
    }

    #[test]
    fn test_primary_plugins_skyrimse_reads_ccc() {
        let tmp = tempfile::tempdir().unwrap();
        std::fs::write(
            tmp.path().join("Skyrim.ccc"),
            "ccbgssse001-fish.esm\n#comment\nCCBGSSSE001-fish.esm\n",
        )
        .unwrap();
        let def = GameDef::from_id(GameId::SkyrimSE, tmp.path().to_path_buf()).unwrap();
        let primary = def.primary_plugins();
        assert!(primary
            .iter()
            .any(|p| p.to_lowercase() == "ccbgssse001-fish.esm"));
        let count = primary
            .iter()
            .filter(|p| p.to_lowercase() == "ccbgssse001-fish.esm")
            .count();
        assert_eq!(count, 1);
    }

    #[test]
    fn test_all_games_have_data_dir() {
        for meta in KNOWN_GAMES {
            assert!(
                !meta.data_dir_name.is_empty(),
                "Game {} has empty data_dir_name",
                meta.name
            );
        }
    }

    #[test]
    fn test_all_games_have_binary() {
        for meta in KNOWN_GAMES {
            assert!(
                meta.binary_name.ends_with(".exe"),
                "Game {} binary doesn't end with .exe: {}",
                meta.name,
                meta.binary_name
            );
        }
    }

    #[test]
    fn test_gog_ids() {
        // Only some games have GOG IDs
        let nv = find_meta_by_id(GameId::FalloutNV).unwrap();
        assert!(nv.gog_app_id.is_some());

        let sse = find_meta_by_id(GameId::SkyrimSE).unwrap();
        assert!(sse.gog_app_id.is_none());
    }
}

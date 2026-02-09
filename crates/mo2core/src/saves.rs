//! Save file scanning.
//!
//! Scans for game save files. Supports Bethesda save formats (.ess for Skyrim,
//! .fos for Fallout) and other common save extensions.
//!
//! MO2 supports profile-local saves: when enabled, saves are stored in the
//! profile directory instead of the game's save folder.

use std::path::{Path, PathBuf};
use std::time::SystemTime;

/// Information about a save file.
#[derive(Debug, Clone)]
pub struct SaveInfo {
    /// Save filename
    pub filename: String,
    /// Full path to the save file
    pub path: PathBuf,
    /// Character/player name (extracted from filename heuristic)
    pub character: String,
    /// File size in bytes
    pub size: u64,
    /// Last modified time
    pub modified: SystemTime,
}

/// Known save file extensions by game.
const SAVE_EXTENSIONS: &[&str] = &[
    "ess", // Skyrim
    "fos", // Fallout 3/NV/4
    "sav", // Generic (Starfield, etc.)
    "bak", // Save backups
];

/// Scan a directory for save files.
pub fn scan_saves(save_dir: &Path) -> anyhow::Result<Vec<SaveInfo>> {
    let mut saves = Vec::new();

    if !save_dir.exists() {
        return Ok(saves);
    }

    for entry in std::fs::read_dir(save_dir)? {
        let entry = entry?;
        let path = entry.path();

        if !path.is_file() {
            continue;
        }

        let ext = path
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.to_lowercase())
            .unwrap_or_default();

        if !SAVE_EXTENSIONS.contains(&ext.as_str()) {
            continue;
        }

        let filename = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("")
            .to_string();

        let metadata = entry.metadata()?;
        let size = metadata.len();
        let modified = metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH);

        // Heuristic: extract character name from filename
        // Common patterns: "Save 1 - CharName ...", "CharName - Save 1", etc.
        let character = extract_character_name(&filename);

        saves.push(SaveInfo {
            filename,
            path,
            character,
            size,
            modified,
        });
    }

    // Sort by modified time (newest first)
    saves.sort_by(|a, b| b.modified.cmp(&a.modified));
    Ok(saves)
}

/// Try to extract a character name from a save filename.
///
/// Handles multiple Bethesda save naming conventions:
/// - Classic: "Save N - CharacterName, Location, HH.MM, Level NN, DD.MM.YYYY"
/// - Skyrim SE/modded: "SaveN_HASH_0_HexCharName_Location_Level_Timestamp_?_?"
fn extract_character_name(filename: &str) -> String {
    let stem = filename
        .rsplit_once('.')
        .map(|(name, _ext)| name)
        .unwrap_or(filename);

    // Try "Save N - CharName, ..." pattern (classic Bethesda)
    if let Some(after_dash) = stem.split(" - ").nth(1) {
        if let Some(name) = after_dash.split(',').next() {
            let trimmed = name.trim();
            if !trimmed.is_empty() {
                return trimmed.to_string();
            }
        }
    }

    // Try underscore-separated Skyrim SE format:
    // SaveN_HASH_0_HexCharName_Location_...
    // The 4th field (index 3) is a hex-encoded character name.
    let parts: Vec<&str> = stem.splitn(6, '_').collect();
    if parts.len() >= 5
        && parts[0]
            .strip_prefix("Save")
            .or_else(|| parts[0].strip_prefix("Quicksave"))
            .or_else(|| parts[0].strip_prefix("Autosave"))
            .is_some()
    {
        if let Some(decoded) = decode_hex_string(parts[3]) {
            if !decoded.is_empty() {
                return decoded;
            }
        }
    }

    // Fallback: use filename stem
    stem.to_string()
}

/// Decode a hex-encoded ASCII/UTF-8 string (e.g. "4C696C6C697468" → "Lillith").
fn decode_hex_string(hex: &str) -> Option<String> {
    if hex.len() < 2 || !hex.len().is_multiple_of(2) {
        return None;
    }
    // Verify all chars are hex digits
    if !hex.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    let bytes: Vec<u8> = (0..hex.len())
        .step_by(2)
        .filter_map(|i| u8::from_str_radix(&hex[i..i + 2], 16).ok())
        .collect();
    if bytes.len() != hex.len() / 2 {
        return None;
    }
    String::from_utf8(bytes).ok()
}

/// Determine the save directory for an instance.
///
/// If profile has local saves enabled, use `<profile_dir>/saves/`.
/// Otherwise, look in the Fluorine prefix or game directory for standard save locations.
pub fn resolve_save_dir(
    profile_path: &Path,
    local_saves: bool,
    game_name: Option<&str>,
    game_dir: Option<&Path>,
    prefix_path: Option<&Path>,
) -> Option<PathBuf> {
    if local_saves {
        return Some(profile_path.join("saves"));
    }

    // Try to find saves in the Wine prefix (Fluorine prefix takes priority)
    if let Some(prefix) = prefix_path {
        let my_games = prefix.join("drive_c/users/steamuser/Documents/My Games");
        if my_games.exists() {
            if let Some(name) = game_name {
                let game_saves = my_games.join(name).join("Saves");
                if game_saves.exists() {
                    return Some(game_saves);
                }
            }
            // List game dirs to find one with saves
            if let Ok(entries) = std::fs::read_dir(&my_games) {
                for entry in entries.flatten() {
                    let saves = entry.path().join("Saves");
                    if saves.exists() {
                        return Some(saves);
                    }
                }
            }
        }
    }

    // Fallback: walk up from game directory looking for pfx/drive_c structure
    if let Some(game_path) = game_dir {
        let mut current = game_path;
        while let Some(parent) = current.parent() {
            let pfx_saves = parent.join("pfx/drive_c/users/steamuser/Documents/My Games");
            if pfx_saves.exists() {
                if let Some(name) = game_name {
                    let game_saves = pfx_saves.join(name).join("Saves");
                    if game_saves.exists() {
                        return Some(game_saves);
                    }
                }
                if let Ok(entries) = std::fs::read_dir(&pfx_saves) {
                    for entry in entries.flatten() {
                        let saves = entry.path().join("Saves");
                        if saves.exists() {
                            return Some(saves);
                        }
                    }
                }
            }
            current = parent;
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_scan_empty() {
        let tmp = tempfile::tempdir().unwrap();
        let saves = scan_saves(tmp.path()).unwrap();
        assert!(saves.is_empty());
    }

    #[test]
    fn test_scan_saves() {
        let tmp = tempfile::tempdir().unwrap();
        std::fs::write(tmp.path().join("Save 1 - Dragonborn, Whiterun.ess"), "save").unwrap();
        std::fs::write(tmp.path().join("quicksave.ess"), "save").unwrap();
        std::fs::write(tmp.path().join("screenshot.png"), "not a save").unwrap();

        let saves = scan_saves(tmp.path()).unwrap();
        assert_eq!(saves.len(), 2);
    }

    #[test]
    fn test_extract_character_name() {
        // Classic Bethesda format
        assert_eq!(
            extract_character_name("Save 1 - Dragonborn, Whiterun, 14.30.ess"),
            "Dragonborn"
        );
        assert_eq!(extract_character_name("quicksave.ess"), "quicksave");

        // Skyrim SE underscore-separated format with hex-encoded name
        // 4C696C6C697468 = "Lillith"
        assert_eq!(
            extract_character_name(
                "Save10_84A08788_0_4C696C6C697468_BluePalaceWingWorld_000041_20260208072953_1_1.ess"
            ),
            "Lillith"
        );
    }

    #[test]
    fn test_scan_nonexistent() {
        let saves = scan_saves(Path::new("/nonexistent")).unwrap();
        assert!(saves.is_empty());
    }

    #[test]
    fn test_local_saves_dir() {
        let tmp = tempfile::tempdir().unwrap();
        let profile = tmp.path().join("Default");
        std::fs::create_dir_all(&profile).unwrap();

        let dir = resolve_save_dir(&profile, true, None, None, None);
        assert_eq!(dir, Some(profile.join("saves")));
    }
}

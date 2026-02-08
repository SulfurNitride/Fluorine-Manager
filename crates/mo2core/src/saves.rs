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
/// Bethesda save naming: "Save N - CharacterName, Location, HH.MM, Level NN, DD.MM.YYYY"
fn extract_character_name(filename: &str) -> String {
    let stem = filename.rsplit('.').last().unwrap_or(filename);

    // Try "Save N - CharName, ..." pattern
    if let Some(after_dash) = stem.split(" - ").nth(1) {
        if let Some(name) = after_dash.split(',').next() {
            let trimmed = name.trim();
            if !trimmed.is_empty() {
                return trimmed.to_string();
            }
        }
    }

    // Fallback: use filename stem
    stem.to_string()
}

/// Determine the save directory for an instance.
///
/// If profile has local saves enabled, use `<profile_dir>/saves/`.
/// Otherwise, look in the game's standard save location within the Wine prefix.
pub fn resolve_save_dir(
    profile_path: &Path,
    local_saves: bool,
    game_name: Option<&str>,
    game_dir: Option<&Path>,
) -> Option<PathBuf> {
    if local_saves {
        return Some(profile_path.join("saves"));
    }

    // Try to find saves in the game's documents folder
    // For Bethesda games under Wine/Proton, saves are typically in:
    //   <prefix>/drive_c/users/steamuser/Documents/My Games/<GameName>/Saves
    // or next to the game directory
    if let Some(game_path) = game_dir {
        // Check for pfx/drive_c structure (Steam/Proton)
        // Walk up to find compatdata or steamapps
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
                // List game dirs to find one with saves
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
        assert_eq!(
            extract_character_name("Save 1 - Dragonborn, Whiterun, 14.30.ess"),
            "Dragonborn"
        );
        assert_eq!(extract_character_name("quicksave.ess"), "quicksave");
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

        let dir = resolve_save_dir(&profile, true, None, None);
        assert_eq!(dir, Some(profile.join("saves")));
    }
}

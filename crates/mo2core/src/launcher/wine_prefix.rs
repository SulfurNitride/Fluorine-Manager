use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;

use anyhow::{bail, Context, Result};

/// A Wine/Proton prefix directory.
#[derive(Debug)]
pub struct WinePrefix {
    pub path: PathBuf,
}

impl WinePrefix {
    /// Load an existing prefix, verifying `drive_c/` exists.
    pub fn load(path: &Path) -> Result<Self> {
        let drive_c = path.join("drive_c");
        if !drive_c.exists() {
            bail!("Not a valid Wine prefix: {} (no drive_c/)", path.display());
        }
        Ok(Self {
            path: path.to_path_buf(),
        })
    }

    /// Locate a Steam game's prefix from its App ID.
    pub fn from_steam_game(app_id: u32) -> Result<Self> {
        let steam_path =
            nak_rust::steam::find_steam_path().context("Steam installation not found")?;
        let prefix = steam_path
            .join("steamapps/compatdata")
            .join(app_id.to_string())
            .join("pfx");
        Self::load(&prefix)
    }

    /// Path to drive_c inside the prefix.
    pub fn drive_c(&self) -> PathBuf {
        self.path.join("drive_c")
    }

    /// Path to the user's Documents folder inside the prefix.
    pub fn documents_path(&self) -> PathBuf {
        self.drive_c().join("users/steamuser/Documents")
    }

    /// Path to My Games inside the prefix.
    pub fn my_games_path(&self) -> PathBuf {
        self.documents_path().join("My Games")
    }

    /// Path to AppData/Local inside the prefix.
    pub fn appdata_local(&self) -> PathBuf {
        self.drive_c().join("users/steamuser/AppData/Local")
    }
}

/// Deploy plugins.txt and loadorder.txt to the game's AppData/Local folder inside the Wine prefix.
///
/// Bethesda games read their plugin load order from:
///   `AppData/Local/<game_name>/Plugins.txt`
///   `AppData/Local/<game_name>/loadorder.txt`
///
/// Format: `*PluginName.esp` (enabled), `PluginName.esp` (disabled)
pub fn deploy_plugins(
    prefix: &WinePrefix,
    appdata_folder: &str,
    plugins: &[(String, bool)], // (filename, enabled)
) -> Result<()> {
    let plugins_dir = prefix.appdata_local().join(appdata_folder);
    std::fs::create_dir_all(&plugins_dir)
        .with_context(|| format!("Failed to create AppData dir {:?}", plugins_dir))?;

    let plugins_path = plugins_dir.join("Plugins.txt");
    let loadorder_path = plugins_dir.join("loadorder.txt");

    let mut content = String::new();
    content
        .push_str("# This file is used by the game to keep track of your downloaded content.\r\n");
    content.push_str("# Please do not modify this file.\r\n");

    for (filename, enabled) in plugins {
        if *enabled {
            content.push('*');
        }
        content.push_str(filename);
        content.push_str("\r\n");
    }

    std::fs::write(&plugins_path, &content)
        .with_context(|| format!("Failed to write {:?}", plugins_path))?;

    // loadorder.txt is an ordered plugin list without enable markers.
    // Keep ordering exactly as provided by the caller.
    let mut loadorder_content = String::new();
    for (filename, _) in plugins {
        loadorder_content.push_str(filename);
        loadorder_content.push_str("\r\n");
    }

    std::fs::write(&loadorder_path, &loadorder_content)
        .with_context(|| format!("Failed to write {:?}", loadorder_path))?;

    tracing::info!(
        "Deployed plugins.txt + loadorder.txt ({} plugins) to {:?}",
        plugins.len(),
        plugins_path
    );

    Ok(())
}

/// Enforce safer Skyrim display defaults in the active prefix.
///
/// This avoids a common Proton/Wayland black-screen startup mode by ensuring:
/// - `bBorderless=1`
/// - `bFull Screen=0`
pub fn enforce_skyrim_window_mode(prefix: &WinePrefix, my_games_folder: &str) -> Result<()> {
    let prefs_path = prefix
        .my_games_path()
        .join(my_games_folder)
        .join("SkyrimPrefs.ini");
    if !prefs_path.exists() {
        return Ok(());
    }

    let content = std::fs::read_to_string(&prefs_path)
        .with_context(|| format!("Failed to read {:?}", prefs_path))?;
    let updated = upsert_ini_key(
        &upsert_ini_key(&content, "Display", "bBorderless", "1"),
        "Display",
        "bFull Screen",
        "0",
    );

    if updated != content {
        std::fs::write(&prefs_path, updated)
            .with_context(|| format!("Failed to write {:?}", prefs_path))?;
        tracing::info!("Applied safe Skyrim display mode in {:?}", prefs_path);
    }

    Ok(())
}

fn upsert_ini_key(content: &str, section: &str, key: &str, value: &str) -> String {
    let section_header = format!("[{section}]");
    let key_lower = key.to_lowercase();
    let mut out: Vec<String> = Vec::new();
    let mut in_target_section = false;
    let mut section_seen = false;
    let mut key_written = false;

    for line in content.lines() {
        let trimmed = line.trim();
        let is_section = trimmed.starts_with('[') && trimmed.ends_with(']');
        if is_section {
            if in_target_section && !key_written {
                out.push(format!("{key}={value}"));
                key_written = true;
            }
            in_target_section = trimmed.eq_ignore_ascii_case(&section_header);
            if in_target_section {
                section_seen = true;
            }
            out.push(line.to_string());
            continue;
        }

        if in_target_section {
            let lower = trimmed.to_lowercase();
            if lower.starts_with(&format!("{key_lower}=")) {
                out.push(format!("{key}={value}"));
                key_written = true;
                continue;
            }
        }

        out.push(line.to_string());
    }

    if section_seen {
        if in_target_section && !key_written {
            out.push(format!("{key}={value}"));
        }
    } else {
        if !out.is_empty() && !out.last().is_some_and(|l| l.is_empty()) {
            out.push(String::new());
        }
        out.push(section_header);
        out.push(format!("{key}={value}"));
    }

    out.join("\n")
}

/// Deploy profile-local INI files to the Wine prefix's My Games folder.
///
/// When `LocalSettings=true`, the profile directory contains copies of the game's
/// INI files (e.g., Skyrim.ini, SkyrimPrefs.ini). These are copied to the prefix's
/// `Documents/My Games/<game>/` so the game reads profile-specific settings.
pub fn deploy_profile_ini(
    prefix: &WinePrefix,
    my_games_folder: &str,
    profile_path: &Path,
    ini_files: &[String],
) -> Result<()> {
    let target_dir = prefix.my_games_path().join(my_games_folder);
    std::fs::create_dir_all(&target_dir)
        .with_context(|| format!("Failed to create My Games dir {:?}", target_dir))?;

    let mut deployed = 0;
    for ini_name in ini_files {
        // Case-insensitive lookup: profile may store INIs as lowercase (e.g. "skyrim.ini")
        // while game_def uses mixed case (e.g. "Skyrim.ini"). Linux is case-sensitive.
        let src = find_file_case_insensitive(profile_path, ini_name)
            .unwrap_or_else(|| profile_path.join(ini_name));
        if src.exists() {
            // Write to prefix using the canonical name the game expects
            let dest = target_dir.join(ini_name);
            std::fs::copy(&src, &dest)
                .with_context(|| format!("Failed to copy {:?} -> {:?}", src, dest))?;
            deployed += 1;
        }
    }

    if deployed > 0 {
        tracing::info!(
            "Deployed {} profile INI files to {:?}",
            deployed,
            target_dir
        );
    }

    Ok(())
}

/// Sync INI files back from the Wine prefix to the profile directory.
///
/// Called after a launched process exits when `LocalSettings=true`.
/// Copies any modified INI files from the prefix back to the profile.
pub fn sync_ini_back_to_profile(
    prefix: &WinePrefix,
    my_games_folder: &str,
    profile_path: &Path,
    ini_files: &[String],
) -> Result<()> {
    let source_dir = prefix.my_games_path().join(my_games_folder);
    if !source_dir.exists() {
        return Ok(());
    }

    let mut synced = 0;
    for ini_name in ini_files {
        // Case-insensitive lookup in prefix (game may write as "Skyrim.INI" etc.)
        let src = find_file_case_insensitive(&source_dir, ini_name)
            .unwrap_or_else(|| source_dir.join(ini_name));
        if src.exists() {
            // Write back using the filename already in the profile (preserves its casing)
            let dest = find_file_case_insensitive(profile_path, ini_name)
                .unwrap_or_else(|| profile_path.join(ini_name));
            std::fs::copy(&src, &dest)
                .with_context(|| format!("Failed to sync back {:?} -> {:?}", src, dest))?;
            synced += 1;
        }
    }

    if synced > 0 {
        tracing::info!(
            "Synced {} INI files back to profile {:?}",
            synced,
            profile_path
        );
    }

    Ok(())
}

/// Deploy profile-local saves to the Wine prefix's saves folder.
///
/// When `LocalSaves=true`, saves are stored in `<profile>/saves/`.
/// This creates a symlink from the prefix's save location to the profile saves dir,
/// so the game reads/writes saves directly to the profile.
pub fn deploy_profile_saves(
    prefix: &WinePrefix,
    my_games_folder: &str,
    profile_path: &Path,
) -> Result<()> {
    let profile_saves = profile_path.join("saves");
    std::fs::create_dir_all(&profile_saves)
        .with_context(|| format!("Failed to create profile saves dir {:?}", profile_saves))?;

    let target_dir = prefix.my_games_path().join(my_games_folder);
    std::fs::create_dir_all(&target_dir)
        .with_context(|| format!("Failed to create My Games dir {:?}", target_dir))?;

    let saves_in_prefix = target_dir.join("Saves");

    // If the saves dir in the prefix already exists and is NOT a symlink,
    // move existing saves to the profile saves dir first.
    if saves_in_prefix.exists() && !saves_in_prefix.is_symlink() {
        if saves_in_prefix.is_dir() {
            // Move any existing save files to profile saves
            if let Ok(entries) = std::fs::read_dir(&saves_in_prefix) {
                for entry in entries.flatten() {
                    let dest = profile_saves.join(entry.file_name());
                    if !dest.exists() {
                        let _ = std::fs::rename(entry.path(), &dest);
                    }
                }
            }
            std::fs::remove_dir_all(&saves_in_prefix)
                .with_context(|| format!("Failed to remove {:?}", saves_in_prefix))?;
        }
    }

    // Create symlink: prefix Saves/ -> profile saves/
    if !saves_in_prefix.exists() {
        std::os::unix::fs::symlink(&profile_saves, &saves_in_prefix).with_context(|| {
            format!(
                "Failed to symlink {:?} -> {:?}",
                saves_in_prefix, profile_saves
            )
        })?;
        tracing::info!(
            "Symlinked saves: {:?} -> {:?}",
            saves_in_prefix,
            profile_saves
        );
    }

    Ok(())
}

/// Initialize a Wine prefix by running `proton run wineboot -u`.
///
/// This creates the full prefix structure (drive_c/, registry, etc.)
/// that Steam/Proton expects. Must be called after creating the Steam
/// shortcut via `add_mod_manager_shortcut()`.
pub fn initialize_prefix(prefix_path: &Path, proton_path: &Path, app_id: u32) -> Result<()> {
    let proton_script = proton_path.join("proton");
    if !proton_script.exists() {
        bail!("Proton script not found at {}", proton_script.display());
    }

    let steam_root = nak_rust::steam::find_steam_path().context("Steam installation not found")?;

    // STEAM_COMPAT_DATA_PATH is the parent of pfx/ (the compatdata/<appid>/ dir)
    let compat_data = prefix_path
        .parent()
        .context("Could not determine compatdata path from prefix")?;

    tracing::info!(
        "Initializing prefix: proton={}, compat_data={}",
        proton_script.display(),
        compat_data.display()
    );

    let status = Command::new(&proton_script)
        .args(["run", "wineboot", "-u"])
        .env("STEAM_COMPAT_CLIENT_INSTALL_PATH", &steam_root)
        .env("STEAM_COMPAT_DATA_PATH", compat_data)
        .env("SteamAppId", app_id.to_string())
        .env("SteamGameId", app_id.to_string())
        .env("DISPLAY", "")
        .env("WAYLAND_DISPLAY", "")
        .env("WINEDEBUG", "-all")
        .env("WINEDLLOVERRIDES", "msdia80.dll=n;conhost.exe=d;cmd.exe=d")
        .status()
        .context("Failed to run proton wineboot")?;

    if !status.success() {
        bail!("proton wineboot failed with exit code: {:?}", status.code());
    }

    // Give files time to land
    std::thread::sleep(Duration::from_secs(2));

    // Verify prefix was created
    if !prefix_path.join("drive_c").exists() {
        bail!(
            "Prefix directory not created after wineboot at {}",
            prefix_path.display()
        );
    }

    tracing::info!(
        "Prefix initialized successfully at {}",
        prefix_path.display()
    );
    Ok(())
}

/// Find a file in a directory by case-insensitive name match.
///
/// Linux filesystems are case-sensitive, but MO2 profiles may store INI files
/// as lowercase ("skyrim.ini") while the game expects mixed case ("Skyrim.ini").
fn find_file_case_insensitive(dir: &Path, target_name: &str) -> Option<PathBuf> {
    let target_lower = target_name.to_lowercase();
    // Fast path: exact match
    let exact = dir.join(target_name);
    if exact.exists() {
        return Some(exact);
    }
    // Slow path: scan directory for case-insensitive match
    let entries = std::fs::read_dir(dir).ok()?;
    for entry in entries.flatten() {
        if let Some(name) = entry.file_name().to_str() {
            if name.to_lowercase() == target_lower {
                return Some(entry.path());
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_load_missing_drive_c() {
        let dir = tempfile::tempdir().unwrap();
        let result = WinePrefix::load(dir.path());
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("no drive_c"));
    }

    #[test]
    fn test_load_valid_prefix() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("drive_c")).unwrap();
        let prefix = WinePrefix::load(dir.path()).unwrap();
        assert!(prefix.drive_c().exists());
    }

    #[test]
    fn test_path_helpers() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("drive_c")).unwrap();
        let prefix = WinePrefix::load(dir.path()).unwrap();

        assert_eq!(prefix.drive_c(), dir.path().join("drive_c"));
        assert_eq!(
            prefix.documents_path(),
            dir.path().join("drive_c/users/steamuser/Documents")
        );
        assert_eq!(
            prefix.my_games_path(),
            dir.path()
                .join("drive_c/users/steamuser/Documents/My Games")
        );
        assert_eq!(
            prefix.appdata_local(),
            dir.path().join("drive_c/users/steamuser/AppData/Local")
        );
    }

    #[test]
    fn test_deploy_plugins() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("drive_c")).unwrap();
        let prefix = WinePrefix::load(dir.path()).unwrap();

        let plugins = vec![
            ("Skyrim.esm".to_string(), true),
            ("Update.esm".to_string(), true),
            ("MyMod.esp".to_string(), true),
            ("Disabled.esp".to_string(), false),
        ];

        deploy_plugins(&prefix, "Skyrim Special Edition", &plugins).unwrap();

        let plugins_path = prefix
            .appdata_local()
            .join("Skyrim Special Edition/Plugins.txt");
        let loadorder_path = prefix
            .appdata_local()
            .join("Skyrim Special Edition/loadorder.txt");
        assert!(plugins_path.exists());
        assert!(loadorder_path.exists());

        let plugins_content = std::fs::read_to_string(&plugins_path).unwrap();
        assert!(plugins_content.contains("*Skyrim.esm"));
        assert!(plugins_content.contains("*Update.esm"));
        assert!(plugins_content.contains("*MyMod.esp"));
        // Disabled plugin should NOT have * prefix
        assert!(plugins_content.contains("Disabled.esp"));
        assert!(!plugins_content.contains("*Disabled.esp"));

        let loadorder_content = std::fs::read_to_string(&loadorder_path).unwrap();
        assert!(loadorder_content.contains("Skyrim.esm"));
        assert!(loadorder_content.contains("Update.esm"));
        assert!(loadorder_content.contains("MyMod.esp"));
        assert!(loadorder_content.contains("Disabled.esp"));
        assert!(!loadorder_content.contains("*"));
    }

    #[test]
    fn test_upsert_ini_key_updates_or_adds() {
        let content = "[Display]\nbBorderless=0\n";
        let updated = upsert_ini_key(content, "Display", "bBorderless", "1");
        assert!(updated.contains("bBorderless=1"));

        let added = upsert_ini_key(updated.as_str(), "Display", "bFull Screen", "0");
        assert!(added.contains("bFull Screen=0"));

        let no_section = "foo=bar\n";
        let added_section = upsert_ini_key(no_section, "Display", "bBorderless", "1");
        assert!(added_section.contains("[Display]"));
        assert!(added_section.contains("bBorderless=1"));
    }
}

//! Steam Proton detection and management
//!
//! Finds Protons that Steam can see and use for non-Steam games.
//! This includes Steam's built-in Protons and custom Protons in compatibilitytools.d.

use std::fs;
use std::path::PathBuf;

use super::find_steam_path;

/// Information about an installed Proton version
#[derive(Debug, Clone)]
pub struct SteamProton {
    /// Display name (e.g., "GE-Proton9-20", "Proton Experimental")
    pub name: String,
    /// Internal name used in config.vdf (e.g., "proton_experimental", "GE-Proton9-20")
    pub config_name: String,
    /// Full path to the Proton installation
    pub path: PathBuf,
    /// Whether this is a Steam-provided Proton (vs custom)
    pub is_steam_proton: bool,
    /// Whether this is Proton Experimental
    pub is_experimental: bool,
}

impl SteamProton {
    /// Get the path to the wine binary.
    pub fn wine_binary(&self) -> Option<PathBuf> {
        let paths = [
            self.path.join("files/bin/wine"),
            self.path.join("dist/bin/wine"),
        ];
        paths.into_iter().find(|p| p.exists())
    }

    /// Get the path to the wineserver binary.
    pub fn wineserver_binary(&self) -> Option<PathBuf> {
        let paths = [
            self.path.join("files/bin/wineserver"),
            self.path.join("dist/bin/wineserver"),
        ];
        paths.into_iter().find(|p| p.exists())
    }

    /// Get the bin directory containing wine executables.
    pub fn bin_dir(&self) -> Option<PathBuf> {
        self.wine_binary().and_then(|p| p.parent().map(|p| p.to_path_buf()))
    }
}

/// Find all Protons that Steam can use (Proton 10+ only)
pub fn find_steam_protons() -> Vec<SteamProton> {
    let mut protons = Vec::new();

    let Some(steam_path) = find_steam_path() else {
        return protons;
    };

    // 1. Steam's built-in Protons (steamapps/common/Proton*)
    protons.extend(find_builtin_protons(&steam_path));

    // 2. Custom Protons in user's compatibilitytools.d
    protons.extend(find_custom_protons(&steam_path));

    // 3. System-level Protons in /usr/share/steam/compatibilitytools.d/
    //    (Arch packages Proton here; Flatpak has --filesystem=/usr/share/steam:ro)
    protons.extend(find_system_protons());

    // Filter to only include Proton 10+ (required for Steam-native integration)
    protons.retain(is_proton_10_or_newer);

    // Filter to only include Protons with valid wine binaries
    protons.retain(|p| {
        let has_wine = p.wine_binary().is_some();
        if !has_wine {
            crate::logging::log_warning(&format!(
                "Skipping Proton '{}': wine binary not found at expected paths (files/bin/wine or dist/bin/wine)",
                p.name
            ));
        }
        has_wine
    });

    // Sort: Experimental first, then by name descending (newest first)
    protons.sort_by(|a, b| {
        if a.is_experimental != b.is_experimental {
            return b.is_experimental.cmp(&a.is_experimental);
        }
        b.name.cmp(&a.name)
    });

    protons
}

/// Check if a Proton version is 10 or newer
fn is_proton_10_or_newer(proton: &SteamProton) -> bool {
    let name = &proton.name;

    if proton.is_experimental || name.contains("Experimental") {
        return true;
    }

    if name.contains("CachyOS") {
        return true;
    }

    if name == "LegacyRuntime" || name.contains("Runtime") {
        return false;
    }

    if name.starts_with("GE-Proton") {
        if let Some(version_part) = name.strip_prefix("GE-Proton") {
            let major: Option<u32> = version_part
                .split('-')
                .next()
                .and_then(|s| s.parse().ok());
            return major.map(|v| v >= 10).unwrap_or(false);
        }
    }

    if name.starts_with("Proton ") {
        if let Some(version_part) = name.strip_prefix("Proton ") {
            let major: Option<u32> = version_part
                .split('.')
                .next()
                .and_then(|s| s.parse().ok());
            return major.map(|v| v >= 10).unwrap_or(false);
        }
    }

    if name.starts_with("EM-") {
        if let Some(version_part) = name.strip_prefix("EM-") {
            let major: Option<u32> = version_part
                .split('.')
                .next()
                .and_then(|s| s.parse().ok());
            return major.map(|v| v >= 10).unwrap_or(false);
        }
    }

    // Unknown format - allow it
    true
}

/// Find Steam's built-in Proton versions
fn find_builtin_protons(steam_path: &std::path::Path) -> Vec<SteamProton> {
    let mut found = Vec::new();
    let common_dir = steam_path.join("steamapps/common");

    let Ok(entries) = fs::read_dir(&common_dir) else {
        return found;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }

        let name = entry.file_name().to_string_lossy().to_string();

        if name.starts_with("Proton") && path.join("proton").exists() {
            let is_experimental = name.contains("Experimental");

            let config_name = if is_experimental {
                "proton_experimental".to_string()
            } else {
                let version = name.replace("Proton ", "");
                let major = version.split('.').next().unwrap_or(&version);
                format!("proton_{}", major)
            };

            found.push(SteamProton {
                name: name.clone(),
                config_name,
                path,
                is_steam_proton: true,
                is_experimental,
            });
        }
    }

    found
}

/// Find custom Protons in compatibilitytools.d
fn find_custom_protons(steam_path: &std::path::Path) -> Vec<SteamProton> {
    let mut found = Vec::new();
    let compat_dir = steam_path.join("compatibilitytools.d");

    let Ok(entries) = fs::read_dir(&compat_dir) else {
        return found;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }

        let name = entry.file_name().to_string_lossy().to_string();

        let has_proton = path.join("proton").exists();
        let has_vdf = path.join("compatibilitytool.vdf").exists();

        if has_proton || has_vdf {
            found.push(SteamProton {
                name: name.clone(),
                config_name: name.clone(),
                path,
                is_steam_proton: false,
                is_experimental: false,
            });
        }
    }

    found
}

/// Find system-level Protons in /usr/share/steam/compatibilitytools.d/
fn find_system_protons() -> Vec<SteamProton> {
    let mut found = Vec::new();
    let system_compat_dir = PathBuf::from("/usr/share/steam/compatibilitytools.d");

    let Ok(entries) = fs::read_dir(&system_compat_dir) else {
        return found;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }

        let name = entry.file_name().to_string_lossy().to_string();

        let has_proton = path.join("proton").exists();
        let has_vdf = path.join("compatibilitytool.vdf").exists();

        if has_proton || has_vdf {
            found.push(SteamProton {
                name: name.clone(),
                config_name: name.clone(),
                path,
                is_steam_proton: false,
                is_experimental: false,
            });
        }
    }

    found
}

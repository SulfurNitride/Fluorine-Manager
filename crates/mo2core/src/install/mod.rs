//! Mod installation pipeline.
//!
//! Handles extracting mod archives, detecting content layout (plain data,
//! FOMOD, BAIN), and installing files into the mods/ directory.

pub mod fomod;

use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{bail, Context, Result};

/// What kind of archive/content layout was detected.
#[derive(Debug, Clone, PartialEq)]
pub enum ContentLayout {
    /// Has a `fomod/ModuleConfig.xml` — needs FOMOD wizard.
    Fomod,
    /// Files directly under a single `Data/` folder (or game-specific variant).
    DataFolder { data_dir_name: String },
    /// BAIN-style numbered folders (00, 01, 02…) containing data files.
    Bain,
    /// Root-level game data files (plugins, textures, etc.) — direct copy.
    RootData,
    /// Unknown structure — let the user decide.
    Unknown,
}

/// A file to install: source path (in staging) → destination (relative to mod dir).
#[derive(Debug, Clone)]
pub struct FileInstallAction {
    pub source: PathBuf,
    pub destination: PathBuf,
}

/// Find the 7z binary (`7zz` or `7z`) on the system.
pub fn find_7z() -> Result<PathBuf> {
    // Try bundled binary next to our executable
    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(exe_dir) = exe_path.parent() {
            for name in &["bin/7zz", "7zz", "bin/7z"] {
                let p = exe_dir.join(name);
                if p.exists() {
                    return Ok(p);
                }
            }
        }
    }

    // Try system PATH
    for name in &["7zz", "7z"] {
        if let Ok(output) = Command::new("which").arg(name).output() {
            if output.status.success() {
                let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
                if !path.is_empty() {
                    return Ok(PathBuf::from(path));
                }
            }
        }
    }

    bail!(
        "7z binary not found. Expected bundled 'bin/7zz' next to mo2gui or system '7zz/7z' in PATH. \
Run scripts/fetch-7zz.sh to bundle 7zz, or install p7zip."
    )
}

/// Extract an archive to a staging directory using the 7z binary.
///
/// Supports all formats 7z handles: zip, 7z, rar, etc.
pub fn extract_archive(archive_path: &Path, staging_dir: &Path) -> Result<()> {
    let sz = find_7z()?;
    std::fs::create_dir_all(staging_dir)?;

    run_7z_extract(&sz, archive_path, staging_dir)?;

    // .tar.gz/.tar.xz/.tar.bz2 often extract to a single .tar first; unpack that too.
    if should_unpack_nested_tar(archive_path) {
        if let Some(inner_tar) = single_top_level_tar(staging_dir) {
            run_7z_extract(&sz, &inner_tar, staging_dir)?;
            let _ = std::fs::remove_file(inner_tar);
        }
    }

    Ok(())
}

fn run_7z_extract(seven_zip_bin: &Path, archive_path: &Path, staging_dir: &Path) -> Result<()> {
    let output = Command::new(seven_zip_bin)
        .arg("x") // extract with full paths
        .arg("-y") // yes to all prompts
        .arg("-aoa") // overwrite existing
        .arg("-scsUTF-8") // UTF-8 filenames
        .arg(format!("-o{}", staging_dir.display()))
        .arg(archive_path)
        .output()
        .with_context(|| format!("Failed to run 7z on {:?}", archive_path))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        bail!("7z extraction failed for {:?}: {}", archive_path, stderr);
    }

    Ok(())
}

fn should_unpack_nested_tar(archive_path: &Path) -> bool {
    let lower = archive_path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    lower.ends_with(".tar.gz") || lower.ends_with(".tar.xz") || lower.ends_with(".tar.bz2")
}

fn single_top_level_tar(staging_dir: &Path) -> Option<PathBuf> {
    let mut files: Vec<PathBuf> = std::fs::read_dir(staging_dir)
        .ok()?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.is_file())
        .collect();

    if files.len() != 1 {
        return None;
    }

    let only = files.remove(0);
    let is_tar = only
        .extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| e.eq_ignore_ascii_case("tar"));

    if is_tar {
        Some(only)
    } else {
        None
    }
}

/// Detect the content layout of an extracted archive.
pub fn detect_layout(staging_dir: &Path, data_dir_name: &str) -> ContentLayout {
    // Check for FOMOD
    let fomod_dir = find_case_insensitive(staging_dir, "fomod");
    if let Some(ref fomod) = fomod_dir {
        if find_case_insensitive(fomod, "ModuleConfig.xml").is_some() {
            return ContentLayout::Fomod;
        }
    }

    // Check for Data/ subfolder
    if let Some(_data_dir) = find_case_insensitive(staging_dir, data_dir_name) {
        return ContentLayout::DataFolder {
            data_dir_name: data_dir_name.to_string(),
        };
    }

    // Check for BAIN structure (numbered dirs like 00, 01, 02)
    if has_bain_structure(staging_dir) {
        return ContentLayout::Bain;
    }

    // Check if files look like game data (plugins, meshes, textures at top level)
    if looks_like_game_data(staging_dir) {
        return ContentLayout::RootData;
    }

    // Check for a single subfolder that contains the actual mod
    // (common: archive has ModName/ as the only top-level dir)
    if let Some(single_dir) = single_top_level_dir(staging_dir) {
        // Recurse into the single directory
        return detect_layout(&single_dir, data_dir_name);
    }

    ContentLayout::Unknown
}

/// Install files from staging into the target mod directory.
///
/// For simple layouts (RootData), copies all files directly.
/// For DataFolder, copies the contents of the data subfolder.
pub fn install_simple(
    staging_dir: &Path,
    mod_dir: &Path,
    layout: &ContentLayout,
    _data_dir_name: &str,
) -> Result<usize> {
    std::fs::create_dir_all(mod_dir)?;

    match layout {
        ContentLayout::DataFolder {
            data_dir_name: name,
        } => {
            let data_src =
                find_case_insensitive(staging_dir, name).unwrap_or_else(|| staging_dir.join(name));
            copy_dir_recursive(&data_src, mod_dir)
        }
        ContentLayout::RootData => copy_dir_recursive(staging_dir, mod_dir),
        ContentLayout::Bain => {
            // Install all numbered dirs that contain data
            let mut count = 0;
            let mut entries: Vec<_> = std::fs::read_dir(staging_dir)?
                .filter_map(|e| e.ok())
                .filter(|e| e.path().is_dir())
                .collect();
            entries.sort_by_key(|e| e.file_name());
            for entry in entries {
                let name = entry.file_name();
                let name_str = name.to_string_lossy();
                // Only include numbered dirs (00, 01, 02) or dirs containing data
                if name_str.chars().next().is_some_and(|c| c.is_ascii_digit())
                    || looks_like_game_data(&entry.path())
                {
                    count += copy_dir_recursive(&entry.path(), mod_dir)?;
                }
            }
            Ok(count)
        }
        ContentLayout::Fomod => {
            bail!("FOMOD archives require the FOMOD wizard — use install_fomod() instead");
        }
        ContentLayout::Unknown => {
            // Fall back to copying everything as-is
            copy_dir_recursive(staging_dir, mod_dir)
        }
    }
}

/// Install files from a resolved FOMOD selection.
pub fn install_fomod_files(
    staging_dir: &Path,
    mod_dir: &Path,
    actions: &[FileInstallAction],
) -> Result<usize> {
    std::fs::create_dir_all(mod_dir)?;
    let mut count = 0;

    for action in actions {
        let src = staging_dir.join(&action.source);
        let dest = mod_dir.join(&action.destination);

        if src.is_dir() {
            count += copy_dir_recursive(&src, &dest)?;
        } else if src.is_file() {
            if let Some(parent) = dest.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::copy(&src, &dest)
                .with_context(|| format!("Failed to copy {:?} -> {:?}", src, dest))?;
            count += 1;
        }
    }

    Ok(count)
}

/// Recursively copy a directory tree.
fn copy_dir_recursive(src: &Path, dest: &Path) -> Result<usize> {
    let mut count = 0;
    for entry in walkdir::WalkDir::new(src)
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let rel = entry.path().strip_prefix(src)?;
        let target = dest.join(rel);

        if entry.file_type().is_dir() {
            std::fs::create_dir_all(&target)?;
        } else {
            if let Some(parent) = target.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::copy(entry.path(), &target)?;
            count += 1;
        }
    }
    Ok(count)
}

/// Case-insensitive path lookup in a directory.
fn find_case_insensitive(dir: &Path, name: &str) -> Option<PathBuf> {
    let lower = name.to_lowercase();
    std::fs::read_dir(dir)
        .ok()?
        .filter_map(|e| e.ok())
        .find(|e| e.file_name().to_string_lossy().to_lowercase() == lower)
        .map(|e| e.path())
}

/// Check for BAIN-style numbered directories.
fn has_bain_structure(dir: &Path) -> bool {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return false;
    };
    let dirs: Vec<_> = entries
        .filter_map(|e| e.ok())
        .filter(|e| e.path().is_dir())
        .collect();
    if dirs.len() < 2 {
        return false;
    }
    // At least 2 dirs must start with digits
    let numbered = dirs
        .iter()
        .filter(|d| {
            d.file_name()
                .to_string_lossy()
                .chars()
                .next()
                .is_some_and(|c| c.is_ascii_digit())
        })
        .count();
    numbered >= 2
}

/// Heuristic: does a directory look like it contains game data files?
pub fn looks_like_game_data(dir: &Path) -> bool {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return false;
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let lower = name.to_string_lossy().to_lowercase();
        // Common game data directories
        if entry.path().is_dir() {
            match lower.as_str() {
                "textures" | "meshes" | "scripts" | "sound" | "interface" | "seq" | "skse"
                | "obse" | "fose" | "nvse" | "f4se" | "sfse" | "strings" | "grass"
                | "shadersfx" | "lodsettings" | "music" | "video" | "terrain" => return true,
                _ => {}
            }
        }
        // Plugin files at top level
        if lower.ends_with(".esp")
            || lower.ends_with(".esm")
            || lower.ends_with(".esl")
            || lower.ends_with(".bsa")
            || lower.ends_with(".ba2")
        {
            return true;
        }
    }
    false
}

/// If a directory has exactly one subdirectory and no files, return that subdirectory.
fn single_top_level_dir(dir: &Path) -> Option<PathBuf> {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return None;
    };
    let entries: Vec<_> = entries.filter_map(|e| e.ok()).collect();
    let dirs: Vec<_> = entries.iter().filter(|e| e.path().is_dir()).collect();
    let files: Vec<_> = entries.iter().filter(|e| e.path().is_file()).collect();

    if dirs.len() == 1 && files.is_empty() {
        Some(dirs[0].path())
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_fomod_layout() {
        let tmp = tempfile::tempdir().unwrap();
        let fomod = tmp.path().join("fomod");
        std::fs::create_dir_all(&fomod).unwrap();
        std::fs::write(fomod.join("ModuleConfig.xml"), "<config/>").unwrap();

        assert_eq!(detect_layout(tmp.path(), "Data"), ContentLayout::Fomod);
    }

    #[test]
    fn test_detect_data_folder() {
        let tmp = tempfile::tempdir().unwrap();
        let data = tmp.path().join("Data");
        std::fs::create_dir_all(data.join("textures")).unwrap();
        std::fs::write(data.join("textures/test.dds"), "tex").unwrap();

        assert_eq!(
            detect_layout(tmp.path(), "Data"),
            ContentLayout::DataFolder {
                data_dir_name: "Data".to_string()
            }
        );
    }

    #[test]
    fn test_detect_root_data() {
        let tmp = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(tmp.path().join("textures")).unwrap();
        std::fs::write(tmp.path().join("textures/test.dds"), "tex").unwrap();
        std::fs::write(tmp.path().join("mod.esp"), "plugin").unwrap();

        assert_eq!(detect_layout(tmp.path(), "Data"), ContentLayout::RootData);
    }

    #[test]
    fn test_detect_bain() {
        let tmp = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(tmp.path().join("00 Base")).unwrap();
        std::fs::create_dir_all(tmp.path().join("01 Optional")).unwrap();
        std::fs::write(tmp.path().join("00 Base/test.esp"), "data").unwrap();

        assert_eq!(detect_layout(tmp.path(), "Data"), ContentLayout::Bain);
    }

    #[test]
    fn test_install_simple_root_data() {
        let tmp = tempfile::tempdir().unwrap();
        let staging = tmp.path().join("staging");
        let mod_dir = tmp.path().join("mod");
        std::fs::create_dir_all(staging.join("textures")).unwrap();
        std::fs::write(staging.join("textures/test.dds"), "tex").unwrap();
        std::fs::write(staging.join("plugin.esp"), "esp").unwrap();

        let count = install_simple(&staging, &mod_dir, &ContentLayout::RootData, "Data").unwrap();
        assert_eq!(count, 2);
        assert!(mod_dir.join("textures/test.dds").exists());
        assert!(mod_dir.join("plugin.esp").exists());
    }

    #[test]
    fn test_install_simple_data_folder() {
        let tmp = tempfile::tempdir().unwrap();
        let staging = tmp.path().join("staging");
        let mod_dir = tmp.path().join("mod");
        std::fs::create_dir_all(staging.join("Data/textures")).unwrap();
        std::fs::write(staging.join("Data/textures/test.dds"), "tex").unwrap();

        let layout = ContentLayout::DataFolder {
            data_dir_name: "Data".to_string(),
        };
        let count = install_simple(&staging, &mod_dir, &layout, "Data").unwrap();
        assert_eq!(count, 1);
        assert!(mod_dir.join("textures/test.dds").exists());
    }

    #[test]
    fn test_case_insensitive_fomod() {
        let tmp = tempfile::tempdir().unwrap();
        let fomod = tmp.path().join("FoMoD");
        std::fs::create_dir_all(&fomod).unwrap();
        std::fs::write(fomod.join("moduleconfig.xml"), "<config/>").unwrap();

        assert_eq!(detect_layout(tmp.path(), "Data"), ContentLayout::Fomod);
    }

    #[test]
    fn test_single_subdir_unwrap() {
        let tmp = tempfile::tempdir().unwrap();
        let sub = tmp.path().join("MyMod");
        std::fs::create_dir_all(sub.join("textures")).unwrap();
        std::fs::write(sub.join("textures/test.dds"), "tex").unwrap();
        std::fs::write(sub.join("plugin.esp"), "esp").unwrap();

        // Should detect as RootData after unwrapping single subdir
        assert_eq!(detect_layout(tmp.path(), "Data"), ContentLayout::RootData);
    }
}

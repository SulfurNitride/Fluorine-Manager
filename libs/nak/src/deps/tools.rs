//! Linux tool management (winetricks, cabextract)
//!
//! Handles downloading and managing Linux CLI tools.
//! Tools are stored in ~/.local/share/fluorine/bin/ for Fluorine Manager.

use std::error::Error;
use std::fs;
use std::io::Read;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::process::Command;

use crate::logging::{log_error, log_info, log_warning};

// ============================================================================
// NaK Bin Directory (~/.local/share/fluorine/bin/)
// ============================================================================

/// Get the tool bin directory path (~/.local/share/fluorine/bin/)
/// This is accessible from both native and Flatpak environments.
pub fn get_nak_bin_path() -> PathBuf {
    crate::paths::data_dir().join("bin")
}

/// Check if a command exists (either in system PATH or tool bin)
pub fn check_command_available(cmd: &str) -> bool {
    // Check system PATH first
    if Command::new("which")
        .arg(cmd)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
    {
        return true;
    }

    // Check tool bin directory
    let nak_bin = get_nak_bin_path().join(cmd);
    nak_bin.exists()
}

// ============================================================================
// Winetricks
// ============================================================================

const WINETRICKS_URL: &str =
    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks";

/// Get the path to winetricks (without downloading)
pub fn get_winetricks_path() -> PathBuf {
    get_nak_bin_path().join("winetricks")
}

/// Ensures winetricks is downloaded and up-to-date.
pub fn ensure_winetricks() -> Result<PathBuf, Box<dyn Error>> {
    let bin_dir = get_nak_bin_path();
    let winetricks_path = bin_dir.join("winetricks");

    fs::create_dir_all(&bin_dir)?;

    log_info("Checking for winetricks updates...");

    match ureq::get(WINETRICKS_URL).call() {
        Ok(response) => {
            let mut new_content = Vec::new();
            response.into_reader().read_to_end(&mut new_content)?;

            let should_update = if winetricks_path.exists() {
                let existing = fs::read(&winetricks_path).unwrap_or_default();
                existing != new_content
            } else {
                true
            };

            if should_update {
                fs::write(&winetricks_path, &new_content)?;

                let mut perms = fs::metadata(&winetricks_path)?.permissions();
                perms.set_mode(0o755);
                fs::set_permissions(&winetricks_path, perms)?;

                if winetricks_path.exists() {
                    log_info("Winetricks updated to latest version");
                } else {
                    log_info(&format!("Winetricks downloaded to {:?}", winetricks_path));
                }
            }
        }
        Err(e) => {
            if winetricks_path.exists() {
                log_warning(&format!("Failed to check winetricks updates: {}", e));
            } else {
                return Err(format!("Failed to download winetricks: {}", e).into());
            }
        }
    }

    Ok(winetricks_path)
}

// ============================================================================
// Cabextract (required by winetricks for DirectX cabs)
// ============================================================================

const CABEXTRACT_URL: &str =
    "https://github.com/SulfurNitride/NaK/releases/download/Cabextract/cabextract-linux-x86_64.zip";

/// Ensures cabextract is available (either system or downloaded).
pub fn ensure_cabextract() -> Result<PathBuf, Box<dyn Error>> {
    // First check if system has cabextract
    if Command::new("which")
        .arg("cabextract")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
    {
        return Ok(PathBuf::from("cabextract"));
    }

    // Check if we already downloaded it
    let bin_dir = get_nak_bin_path();
    let cabextract_path = bin_dir.join("cabextract");

    if cabextract_path.exists() {
        return Ok(cabextract_path);
    }

    // Download cabextract zip
    log_warning("System cabextract not found, downloading...");
    fs::create_dir_all(&bin_dir)?;

    let response = ureq::get(CABEXTRACT_URL).call().map_err(|e| {
        format!(
            "Failed to download cabextract: {}. Please install cabextract manually.",
            e
        )
    })?;

    let zip_path = bin_dir.join("cabextract.zip");
    let mut zip_file = fs::File::create(&zip_path)?;
    std::io::copy(&mut response.into_reader(), &mut zip_file)?;

    let status = Command::new("unzip")
        .arg("-o")
        .arg(&zip_path)
        .arg("-d")
        .arg(&bin_dir)
        .status()?;

    if !status.success() {
        let _ = Command::new("python3")
            .arg("-c")
            .arg(format!(
                "import zipfile; zipfile.ZipFile('{}').extractall('{}')",
                zip_path.display(),
                bin_dir.display()
            ))
            .status();
    }

    let _ = fs::remove_file(&zip_path);

    if cabextract_path.exists() {
        let mut perms = fs::metadata(&cabextract_path)?.permissions();
        perms.set_mode(0o755);
        fs::set_permissions(&cabextract_path, perms)?;
        log_info(&format!("cabextract downloaded to {:?}", cabextract_path));
        Ok(cabextract_path)
    } else {
        log_error("Failed to extract cabextract from zip");
        Err("Failed to extract cabextract from zip".into())
    }
}

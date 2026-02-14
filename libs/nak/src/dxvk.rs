//! DXVK configuration management for Fluorine Manager.
//!
//! Downloads dxvk.conf from upstream, appends Fluorine-specific settings,
//! and stores at `~/.local/share/fluorine/config/dxvk.conf`.

use std::error::Error;
use std::fs;
use std::path::{Path, PathBuf};

use crate::logging::{log_info, log_warning};

const DXVK_CONF_URL: &str =
    "https://raw.githubusercontent.com/doitsujin/dxvk/master/dxvk.conf";

const DXVK_CUSTOM_SETTINGS: &str = r#"
# Fluorine Custom Settings
# Disable Graphics Pipeline Library (can cause issues with modded games)
dxvk.enableGraphicsPipelineLibrary = False
"#;

/// Get the path where the DXVK config will be stored.
pub fn get_dxvk_conf_path() -> PathBuf {
    crate::paths::data_dir().join("config/dxvk.conf")
}

/// Ensure the dxvk.conf file exists, downloading if necessary.
///
/// Returns the path to the config file.
pub fn ensure_dxvk_conf() -> Result<PathBuf, Box<dyn Error>> {
    let conf_path = get_dxvk_conf_path();

    // If it already exists, return it
    if conf_path.exists() {
        return Ok(conf_path);
    }

    download_and_create_dxvk_conf(&conf_path)
}

/// Download the upstream dxvk.conf, append custom settings, and write to `dest`.
pub fn download_and_create_dxvk_conf(dest: &Path) -> Result<PathBuf, Box<dyn Error>> {
    // Ensure parent directory exists
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent)?;
    }

    log_info("Downloading dxvk.conf from upstream...");

    let upstream_content = match ureq::get(DXVK_CONF_URL).call() {
        Ok(response) => {
            let mut body = String::new();
            response.into_reader().read_to_string(&mut body)?;
            body
        }
        Err(e) => {
            log_warning(&format!("Failed to download dxvk.conf: {}", e));
            // Create with just custom settings if download fails
            String::new()
        }
    };

    let full_content = format!("{}\n{}", upstream_content, DXVK_CUSTOM_SETTINGS);
    fs::write(dest, &full_content)?;

    log_info(&format!("Created dxvk.conf at {:?}", dest));
    Ok(dest.to_path_buf())
}

use std::io::Read as _;

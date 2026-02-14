//! Shared data directory for Fluorine Manager.
//!
//! All data lives under `~/.local/share/fluorine/` — accessible from both
//! native and Flatpak builds (the Flatpak has `--filesystem=home`).

use std::path::PathBuf;

/// Returns the Fluorine data directory (`~/.local/share/fluorine`).
pub fn data_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join(".local/share/fluorine")
}

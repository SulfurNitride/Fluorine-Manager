//! Download scanning and metadata.
//!
//! Scans the downloads directory for archive files and their companion `.meta` files
//! (which contain Nexus metadata like mod ID, file ID, etc.).

use std::path::{Path, PathBuf};

use crate::config::meta_ini::MetaIni;

/// Information about a downloaded file.
#[derive(Debug, Clone)]
pub struct DownloadInfo {
    /// Filename of the archive
    pub filename: String,
    /// Full path to the archive
    pub path: PathBuf,
    /// File size in bytes
    pub size: u64,
    /// Nexus mod ID (from companion .meta file)
    pub nexus_id: Option<i64>,
    /// Nexus file ID (from companion .meta file)
    pub file_id: Option<i64>,
    /// Game name (from companion .meta file)
    pub game_name: Option<String>,
    /// Whether this download has been installed (has a matching mod)
    pub installed: bool,
    /// Version string (from companion .meta file)
    pub version: Option<String>,
    /// Mod name (from companion .meta file)
    pub mod_name: Option<String>,
}

/// Known archive extensions for downloads.
const ARCHIVE_EXTENSIONS: &[&str] = &["zip", "7z", "rar", "gz", "xz", "bz2", "tar", "fomod"];

/// Scan a downloads directory for archive files with optional .meta companions.
pub fn scan_downloads(downloads_dir: &Path) -> anyhow::Result<Vec<DownloadInfo>> {
    let mut downloads = Vec::new();

    if !downloads_dir.exists() {
        return Ok(downloads);
    }

    for entry in std::fs::read_dir(downloads_dir)? {
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

        if !ARCHIVE_EXTENSIONS.contains(&ext.as_str()) {
            continue;
        }

        let filename = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("")
            .to_string();

        let metadata = entry.metadata()?;
        let size = metadata.len();

        // Try to read companion .meta file
        let meta_path = path.with_extension(format!("{}.meta", ext));
        let (nexus_id, file_id, game_name, version, mod_name) = if meta_path.exists() {
            match MetaIni::read(&meta_path) {
                Ok(meta) => (
                    meta.mod_id(),
                    meta.file_id(),
                    meta.game_name().map(String::from),
                    meta.version().map(String::from),
                    meta.mod_name().map(String::from),
                ),
                Err(_) => (None, None, None, None, None),
            }
        } else {
            (None, None, None, None, None)
        };

        downloads.push(DownloadInfo {
            filename,
            path,
            size,
            nexus_id,
            file_id,
            game_name,
            version,
            mod_name,
            installed: false, // Will be set by caller comparing against installed mods
        });
    }

    downloads.sort_by(|a, b| a.filename.to_lowercase().cmp(&b.filename.to_lowercase()));
    Ok(downloads)
}

/// Format a file size for display (e.g. "1.5 MB", "320 KB").
pub fn format_size(bytes: u64) -> String {
    if bytes >= 1_073_741_824 {
        format!("{:.1} GB", bytes as f64 / 1_073_741_824.0)
    } else if bytes >= 1_048_576 {
        format!("{:.1} MB", bytes as f64 / 1_048_576.0)
    } else if bytes >= 1024 {
        format!("{:.0} KB", bytes as f64 / 1024.0)
    } else {
        format!("{} B", bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_scan_empty() {
        let tmp = tempfile::tempdir().unwrap();
        let downloads = scan_downloads(tmp.path()).unwrap();
        assert!(downloads.is_empty());
    }

    #[test]
    fn test_scan_archives() {
        let tmp = tempfile::tempdir().unwrap();
        std::fs::write(tmp.path().join("SkyUI-12541-5-2.7z"), "fake archive").unwrap();
        std::fs::write(tmp.path().join("SKSE-30379-2-2-6.zip"), "fake archive").unwrap();
        std::fs::write(tmp.path().join("readme.txt"), "not an archive").unwrap();

        let downloads = scan_downloads(tmp.path()).unwrap();
        assert_eq!(downloads.len(), 2);
        assert_eq!(downloads[0].filename, "SKSE-30379-2-2-6.zip");
        assert_eq!(downloads[1].filename, "SkyUI-12541-5-2.7z");
    }

    #[test]
    fn test_scan_nonexistent() {
        let downloads = scan_downloads(Path::new("/nonexistent")).unwrap();
        assert!(downloads.is_empty());
    }

    #[test]
    fn test_format_size() {
        assert_eq!(format_size(500), "500 B");
        assert_eq!(format_size(1024), "1 KB");
        assert_eq!(format_size(1_048_576), "1.0 MB");
        assert_eq!(format_size(1_573_741_824), "1.5 GB");
    }
}

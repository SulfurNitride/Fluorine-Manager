//! Write-through to overwrite directory.
//!
//! All writes through the VFS are redirected to the overwrite directory,
//! preserving the purity of mod folders. This includes:
//! - New files created by tools/games
//! - Modified existing files (copy-on-write to overwrite)

use std::path::{Path, PathBuf};

use mo2core::paths::ensure_parent_dirs;

/// Manages write-through operations to the overwrite directory.
#[derive(Debug, Clone)]
pub struct OverwriteManager {
    /// Path to the overwrite directory
    pub overwrite_dir: PathBuf,
}

impl OverwriteManager {
    pub fn new(overwrite_dir: &Path) -> Self {
        OverwriteManager {
            overwrite_dir: overwrite_dir.to_path_buf(),
        }
    }

    /// Get the real path in the overwrite directory for a virtual relative path.
    pub fn overwrite_path(&self, relative: &str) -> PathBuf {
        self.overwrite_dir.join(relative)
    }

    /// Write data to a file in the overwrite directory.
    pub fn write_file(&self, relative: &str, data: &[u8]) -> std::io::Result<PathBuf> {
        let path = self.overwrite_path(relative);
        ensure_parent_dirs(&path)?;
        std::fs::write(&path, data)?;
        Ok(path)
    }

    /// Create a directory in the overwrite directory.
    pub fn create_dir(&self, relative: &str) -> std::io::Result<PathBuf> {
        let path = self.overwrite_path(relative);
        std::fs::create_dir_all(&path)?;
        Ok(path)
    }

    /// Remove a file from the overwrite directory.
    pub fn remove_file(&self, relative: &str) -> std::io::Result<()> {
        let path = self.overwrite_path(relative);
        if path.exists() {
            std::fs::remove_file(&path)?;
        }
        Ok(())
    }

    /// Remove a directory from the overwrite directory.
    pub fn remove_dir(&self, relative: &str) -> std::io::Result<()> {
        let path = self.overwrite_path(relative);
        if path.exists() {
            std::fs::remove_dir(&path)?;
        }
        Ok(())
    }

    /// Copy-on-write: copy a file to overwrite before modification.
    pub fn copy_on_write(&self, source: &Path, relative: &str) -> std::io::Result<PathBuf> {
        let dest = self.overwrite_path(relative);
        ensure_parent_dirs(&dest)?;
        std::fs::copy(source, &dest)?;
        Ok(dest)
    }

    /// Rename a file within the overwrite directory.
    pub fn rename(&self, from_relative: &str, to_relative: &str) -> std::io::Result<()> {
        let from = self.overwrite_path(from_relative);
        let to = self.overwrite_path(to_relative);
        ensure_parent_dirs(&to)?;
        std::fs::rename(from, to)?;
        Ok(())
    }

    /// Check if a file exists in the overwrite directory.
    pub fn exists(&self, relative: &str) -> bool {
        self.overwrite_path(relative).exists()
    }

    /// List all files in the overwrite directory (relative paths).
    pub fn list_files(&self) -> Vec<String> {
        let mut files = Vec::new();
        if !self.overwrite_dir.exists() {
            return files;
        }

        for entry in walkdir::WalkDir::new(&self.overwrite_dir)
            .into_iter()
            .filter_map(|e| e.ok())
            .filter(|e| e.file_type().is_file())
        {
            if let Ok(relative) = entry.path().strip_prefix(&self.overwrite_dir) {
                files.push(relative.to_string_lossy().to_string());
            }
        }
        files
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_write_and_read() {
        let tmp = tempfile::tempdir().unwrap();
        let mgr = OverwriteManager::new(tmp.path());

        let path = mgr.write_file("textures/test.dds", b"test data").unwrap();
        assert!(path.exists());
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "test data");
    }

    #[test]
    fn test_create_dir() {
        let tmp = tempfile::tempdir().unwrap();
        let mgr = OverwriteManager::new(tmp.path());

        let path = mgr.create_dir("meshes/actors").unwrap();
        assert!(path.is_dir());
    }

    #[test]
    fn test_copy_on_write() {
        let tmp = tempfile::tempdir().unwrap();
        let source = tmp.path().join("original.txt");
        std::fs::write(&source, "original content").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir(&overwrite).unwrap();
        let mgr = OverwriteManager::new(&overwrite);

        let dest = mgr.copy_on_write(&source, "original.txt").unwrap();
        assert!(dest.exists());
        assert_eq!(std::fs::read_to_string(&dest).unwrap(), "original content");
    }

    #[test]
    fn test_list_files() {
        let tmp = tempfile::tempdir().unwrap();
        let mgr = OverwriteManager::new(tmp.path());

        mgr.write_file("a.txt", b"a").unwrap();
        mgr.write_file("dir/b.txt", b"b").unwrap();

        let files = mgr.list_files();
        assert_eq!(files.len(), 2);
    }

    #[test]
    fn test_remove() {
        let tmp = tempfile::tempdir().unwrap();
        let mgr = OverwriteManager::new(tmp.path());

        mgr.write_file("test.txt", b"data").unwrap();
        assert!(mgr.exists("test.txt"));

        mgr.remove_file("test.txt").unwrap();
        assert!(!mgr.exists("test.txt"));
    }
}

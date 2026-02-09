//! MountManager — manages the FUSE mount lifecycle for the full-game VFS.
//!
//! The VFS merges the entire game directory with mods into a single view at VFS_FUSE/.
//! The real game directory is NEVER modified.
//!
//! Flow:
//! 1. `mount(game_dir, data_dir_name, mods)` — build VFS tree and mount at VFS_FUSE/
//! 2. Game launches from VFS_FUSE/ and sees the merged view
//! 3. `unmount()` — unmount FUSE, flush staging writes to overwrite/

use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};

use anyhow::{Context, Result};

use crate::filesystem::Mo2Filesystem;
use crate::overlay::{self, VfsTree};

/// Manages the FUSE mount lifecycle.
///
/// Mounts a full-game VFS at `<instance>/VFS_FUSE/` that merges:
/// - The real game directory (base layer, untouched)
/// - Mod files under the game's data subdirectory
/// - Root/ mod files at the game root level
/// - Overwrite directory (highest priority)
pub struct MountManager {
    /// Mount point path (e.g., <instance>/VFS_FUSE/)
    mount_point: PathBuf,
    /// Overwrite directory
    overwrite_dir: PathBuf,
    /// Staging directory for COW writes while mounted
    staging_dir: PathBuf,
    /// Shared VFS tree (accessible by UI while mounted)
    tree: Arc<RwLock<VfsTree>>,
    /// Active FUSE session (None if not mounted)
    session: Option<fuser::BackgroundSession>,
}

impl MountManager {
    /// Create a new MountManager.
    ///
    /// `mount_point` — where to mount the VFS (e.g., `<instance>/VFS_FUSE/`)
    /// `overwrite_dir` — the instance's overwrite/ directory
    pub fn new(mount_point: &Path, overwrite_dir: &Path) -> Self {
        let staging_dir = overwrite_dir
            .parent()
            .unwrap_or(overwrite_dir)
            .join("VFS_staging");

        MountManager {
            mount_point: mount_point.to_path_buf(),
            overwrite_dir: overwrite_dir.to_path_buf(),
            staging_dir,
            tree: Arc::new(RwLock::new(VfsTree::new())),
            session: None,
        }
    }

    /// Mount the full-game VFS.
    ///
    /// Builds a VFS tree that merges the entire game directory with mods,
    /// then mounts FUSE at the mount point.
    ///
    /// - `game_dir` — real game installation directory (never modified)
    /// - `data_dir_name` — name of the data subdirectory ("Data", "Data Files")
    /// - `mods` — (mod_name, mod_path) sorted by ascending priority
    pub fn mount(
        &mut self,
        game_dir: &Path,
        data_dir_name: &str,
        mods: &[(&str, &Path)],
    ) -> Result<()> {
        if self.session.is_some() {
            anyhow::bail!("Already mounted");
        }

        // Build the full-game VFS tree
        let new_tree =
            overlay::build_full_game_vfs(game_dir, data_dir_name, mods, &self.overwrite_dir)?;

        {
            let mut tree = self
                .tree
                .write()
                .map_err(|e| anyhow::anyhow!("Lock poisoned: {}", e))?;
            *tree = new_tree;
        }

        // Ensure mount point and staging dir exist
        std::fs::create_dir_all(&self.mount_point)?;
        std::fs::create_dir_all(&self.staging_dir)?;

        // Clean up stale FUSE mount from a previous crash
        crate::FuseController::try_cleanup_stale_mount_at(&self.mount_point);
        // Also clean stale real files in the mount directory (can cause EEXIST on mount).
        self.cleanup_mount_point_contents()?;

        let fs = Mo2Filesystem::new(self.tree.clone(), self.staging_dir.clone());
        let options = vec![
            fuser::MountOption::FSName("mo2linux".to_string()),
            // Offload permission checks to kernel (avoids access() round-trips)
            fuser::MountOption::DefaultPermissions,
            // Disable atime updates (avoids unnecessary write round-trips)
            fuser::MountOption::NoAtime,
        ];

        let session = fuser::spawn_mount2(fs, &self.mount_point, &options)
            .with_context(|| format!("Failed to mount FUSE at {:?}", self.mount_point))?;

        self.session = Some(session);

        tracing::info!(
            "FUSE mounted at {:?} (game: {:?}, staging: {:?})",
            self.mount_point,
            game_dir,
            self.staging_dir
        );

        Ok(())
    }

    /// Unmount FUSE and flush staging writes to overwrite.
    pub fn unmount(&mut self) -> Result<()> {
        if let Some(session) = self.session.take() {
            drop(session);
            tracing::info!("FUSE unmounted from {:?}", self.mount_point);
        }

        // Flush staged writes to overwrite
        self.flush_staging()?;
        // Ensure stale real files don't remain visible in the mount directory.
        self.cleanup_mount_point_contents()?;

        Ok(())
    }

    /// Rebuild the VFS tree without unmounting (live refresh).
    ///
    /// Useful for refreshing the merged view after mod list changes in the UI.
    pub fn rebuild(
        &self,
        game_dir: &Path,
        data_dir_name: &str,
        mods: &[(&str, &Path)],
    ) -> Result<()> {
        let new_tree =
            overlay::build_full_game_vfs(game_dir, data_dir_name, mods, &self.overwrite_dir)?;

        let mut tree = self
            .tree
            .write()
            .map_err(|e| anyhow::anyhow!("Lock poisoned: {}", e))?;
        *tree = new_tree;

        Ok(())
    }

    /// Check if currently mounted.
    pub fn is_mounted(&self) -> bool {
        self.session.is_some()
    }

    /// Get the mount point path.
    pub fn mount_point(&self) -> &Path {
        &self.mount_point
    }

    /// Get a reference to the shared VFS tree (for UI data tab, conflict detection, etc.).
    pub fn tree(&self) -> Arc<RwLock<VfsTree>> {
        self.tree.clone()
    }

    /// Get the path to the game binary within the VFS mount.
    ///
    /// When launching, use this path instead of the real game binary.
    pub fn vfs_binary_path(&self, binary_name: &str) -> PathBuf {
        self.mount_point.join(binary_name)
    }

    /// Get the path to the data directory within the VFS mount.
    pub fn vfs_data_dir(&self, data_dir_name: &str) -> PathBuf {
        self.mount_point.join(data_dir_name)
    }

    /// Move all files from staging directory to overwrite directory.
    fn flush_staging(&self) -> Result<()> {
        if !self.staging_dir.exists() {
            return Ok(());
        }

        let mut moved = 0usize;
        for entry in walkdir::WalkDir::new(&self.staging_dir)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            let relative = match path.strip_prefix(&self.staging_dir) {
                Ok(r) => r,
                Err(_) => continue,
            };

            if relative.as_os_str().is_empty() {
                continue;
            }

            let dest = self.overwrite_dir.join(relative);

            if entry.file_type().is_dir() {
                std::fs::create_dir_all(&dest)?;
            } else if entry.file_type().is_file() {
                if let Some(parent) = dest.parent() {
                    std::fs::create_dir_all(parent)?;
                }
                std::fs::rename(path, &dest).or_else(|_| {
                    std::fs::copy(path, &dest)?;
                    std::fs::remove_file(path)
                })?;
                moved += 1;
            }
        }

        let _ = std::fs::remove_dir_all(&self.staging_dir);

        if moved > 0 {
            tracing::info!("Flushed {} staged file(s) to overwrite", moved);
        }

        Ok(())
    }

    /// Remove any real files left in the mount point after unmount.
    ///
    /// This avoids showing stale on-disk leftovers (for example from earlier
    /// incorrect mounts or writes while not mounted).
    fn cleanup_mount_point_contents(&self) -> Result<()> {
        if !self.mount_point.exists() {
            return Ok(());
        }
        if is_mountpoint(&self.mount_point) {
            return Ok(());
        }
        // Safety guard: only clean dedicated VFS mount directories.
        if self.mount_point.file_name().and_then(|n| n.to_str()) != Some("VFS_FUSE") {
            tracing::warn!(
                "Skipping mount-point cleanup for unexpected path {:?}",
                self.mount_point
            );
            return Ok(());
        }

        for entry in std::fs::read_dir(&self.mount_point)? {
            let path = entry?.path();
            if path.is_dir() {
                std::fs::remove_dir_all(&path)?;
            } else {
                std::fs::remove_file(&path)?;
            }
        }
        Ok(())
    }
}

fn is_mountpoint(path: &Path) -> bool {
    let Ok(mounts) = std::fs::read_to_string("/proc/mounts") else {
        return false;
    };
    let path_str = path.to_string_lossy();
    mounts.lines().any(|line| {
        line.split_whitespace()
            .nth(1)
            .is_some_and(|mp| decode_proc_mount_field(mp) == path_str)
    })
}

/// /proc/mounts escapes spaces and some bytes as octal sequences (e.g. \040).
fn decode_proc_mount_field(field: &str) -> String {
    let mut out = String::with_capacity(field.len());
    let bytes = field.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] == b'\\'
            && i + 3 < bytes.len()
            && bytes[i + 1].is_ascii_digit()
            && bytes[i + 2].is_ascii_digit()
            && bytes[i + 3].is_ascii_digit()
        {
            let oct = &field[i + 1..i + 4];
            if let Ok(v) = u8::from_str_radix(oct, 8) {
                out.push(v as char);
                i += 4;
                continue;
            }
        }
        out.push(bytes[i] as char);
        i += 1;
    }
    out
}

impl Drop for MountManager {
    fn drop(&mut self) {
        if self.session.is_some() {
            tracing::warn!("MountManager dropped while still mounted — attempting cleanup");
            if let Err(e) = self.unmount() {
                tracing::error!("Failed to unmount during drop: {}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_mount_manager_paths() {
        let mm = MountManager::new(
            Path::new("/instance/VFS_FUSE"),
            Path::new("/instance/overwrite"),
        );

        assert_eq!(mm.mount_point(), Path::new("/instance/VFS_FUSE"));
        assert_eq!(mm.staging_dir, PathBuf::from("/instance/VFS_staging"));
        assert!(!mm.is_mounted());
    }

    #[test]
    fn test_vfs_binary_path() {
        let mm = MountManager::new(
            Path::new("/instance/VFS_FUSE"),
            Path::new("/instance/overwrite"),
        );

        assert_eq!(
            mm.vfs_binary_path("SkyrimSE.exe"),
            PathBuf::from("/instance/VFS_FUSE/SkyrimSE.exe")
        );
    }

    #[test]
    fn test_vfs_data_dir() {
        let mm = MountManager::new(
            Path::new("/instance/VFS_FUSE"),
            Path::new("/instance/overwrite"),
        );

        assert_eq!(
            mm.vfs_data_dir("Data"),
            PathBuf::from("/instance/VFS_FUSE/Data")
        );
        assert_eq!(
            mm.vfs_data_dir("Data Files"),
            PathBuf::from("/instance/VFS_FUSE/Data Files")
        );
    }

    #[test]
    fn test_flush_staging() {
        let tmp = tempfile::tempdir().unwrap();
        let mount_point = tmp.path().join("VFS_FUSE");
        let overwrite = tmp.path().join("overwrite");
        let staging = tmp.path().join("VFS_staging");

        std::fs::create_dir_all(&mount_point).unwrap();
        std::fs::create_dir_all(&overwrite).unwrap();

        // Put some files in staging
        std::fs::create_dir_all(staging.join("Data/textures")).unwrap();
        std::fs::write(staging.join("Data/textures/new.dds"), "new texture").unwrap();
        std::fs::write(staging.join("crash_log.txt"), "game crash").unwrap();

        let mm = MountManager::new(&mount_point, &overwrite);
        mm.flush_staging().unwrap();

        // Files should be in overwrite now
        assert!(overwrite.join("Data/textures/new.dds").exists());
        assert!(overwrite.join("crash_log.txt").exists());

        // Staging should be cleaned up
        assert!(!staging.exists());
    }
}

//! FUSE3 virtual filesystem for MO2.
//!
//! Provides a virtual directory that merges mod files by priority,
//! replacing Windows USVFS with Linux-native FUSE3.

pub mod filesystem;
pub mod inode;
pub mod mount_manager;
pub mod overlay;
pub mod overwrite;

use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};

use anyhow::Result;

use crate::overlay::VfsTree;

/// Controls the FUSE mount lifecycle.
pub struct FuseController {
    /// Mount point path
    mount_point: PathBuf,
    /// Shared VFS tree (swappable for live rebuild)
    tree: Arc<RwLock<VfsTree>>,
    /// Overwrite directory (final destination for writes on unmount)
    overwrite_dir: PathBuf,
    /// Staging directory (temporary write target while mounted)
    staging_dir: PathBuf,
    /// FUSE session handle for unmounting
    session: Option<fuser::BackgroundSession>,
}

impl FuseController {
    /// Create a new controller (does not mount yet).
    pub fn new(mount_point: &Path, overwrite_dir: &Path) -> Self {
        let staging_dir = overwrite_dir
            .parent()
            .unwrap_or(overwrite_dir)
            .join("VFS_staging");
        FuseController {
            mount_point: mount_point.to_path_buf(),
            tree: Arc::new(RwLock::new(VfsTree::new())),
            overwrite_dir: overwrite_dir.to_path_buf(),
            staging_dir,
            session: None,
        }
    }

    /// Build or rebuild the VFS tree from active mods.
    ///
    /// `mods` is a list of (mod_name, mod_path) pairs sorted by ascending priority.
    /// The staging directory is included as the highest-priority layer if it has files.
    pub fn rebuild(&self, mods: &[(&str, &Path)]) -> Result<()> {
        let new_tree = overlay::build_vfs_tree(mods, &self.overwrite_dir)?;
        let mut tree = self
            .tree
            .write()
            .map_err(|e| anyhow::anyhow!("Lock poisoned: {}", e))?;
        *tree = new_tree;
        Ok(())
    }

    /// Mount the FUSE filesystem.
    pub fn mount(&mut self) -> Result<()> {
        if self.session.is_some() {
            anyhow::bail!("Already mounted");
        }

        // Ensure mount point and staging dir exist
        std::fs::create_dir_all(&self.mount_point)?;
        std::fs::create_dir_all(&self.staging_dir)?;

        // Clean up stale FUSE mount from a previous crash
        Self::try_cleanup_stale_mount_at(&self.mount_point);

        let fs = filesystem::Mo2Filesystem::new(self.tree.clone(), self.staging_dir.clone());

        let options = vec![
            fuser::MountOption::FSName("mo2linux".to_string()),
            fuser::MountOption::DefaultPermissions,
            fuser::MountOption::NoAtime,
        ];

        let session = fuser::spawn_mount2(fs, &self.mount_point, &options)?;
        self.session = Some(session);

        tracing::info!(
            "FUSE mounted at {:?} (staging at {:?})",
            self.mount_point,
            self.staging_dir
        );
        Ok(())
    }

    /// Unmount the FUSE filesystem and move staging files to overwrite.
    pub fn unmount(&mut self) {
        if let Some(session) = self.session.take() {
            drop(session);
            tracing::info!("FUSE unmounted from {:?}", self.mount_point);

            // Move staged files to overwrite
            if let Err(e) = self.flush_staging() {
                tracing::error!("Failed to flush staging to overwrite: {e}");
            }
        }
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
                    // rename can fail across filesystems, fall back to copy+remove
                    std::fs::copy(path, &dest)?;
                    std::fs::remove_file(path)
                })?;
                moved += 1;
            }
        }

        // Clean up staging directory (remove empty dirs)
        let _ = std::fs::remove_dir_all(&self.staging_dir);

        if moved > 0 {
            tracing::info!("Flushed {} staged file(s) to overwrite", moved);
        }

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

    /// Get a reference to the shared VFS tree.
    pub fn tree(&self) -> Arc<RwLock<VfsTree>> {
        self.tree.clone()
    }

    /// Check if a path is currently a FUSE mount point via /proc/mounts.
    fn is_mountpoint(path: &Path) -> bool {
        let Ok(mounts) = std::fs::read_to_string("/proc/mounts") else {
            return false;
        };
        let path_str = path.to_string_lossy();
        mounts.lines().any(|line| {
            line.split_whitespace()
                .nth(1)
                .is_some_and(|mp| mp == &*path_str)
        })
    }

    /// Try to clean up a stale FUSE mount at a specific path.
    /// Public so MountManager can use it for game Data directory cleanup.
    pub fn try_cleanup_stale_mount_at(mount_point: &Path) {
        if !Self::is_mountpoint(mount_point) {
            return;
        }
        tracing::info!(
            "Stale FUSE mount detected at {:?}, cleaning up",
            mount_point
        );

        // Try normal unmount first, then lazy if that fails.
        // Prefer fusermount3 (modern systems), then fall back to fusermount.
        let try_unmount = |cmd: &str, args: &[&str]| -> bool {
            std::process::Command::new(cmd)
                .args(args)
                .arg(mount_point)
                .output()
                .is_ok_and(|o| o.status.success())
        };

        if !(try_unmount("fusermount3", &["-u"]) || try_unmount("fusermount", &["-u"])) {
            // Fallbacks for distros/tools that don't update /etc/mtab consistently.
            let _ = try_unmount("umount", &[]);
            let _ = try_unmount("umount", &["-l"]);
            let _ = try_unmount("fusermount3", &["-uz"]);
            let _ = try_unmount("fusermount", &["-uz"]);
        }

        // Wait for the kernel to fully release the mount
        for _ in 0..10 {
            if !Self::is_mountpoint(mount_point) {
                tracing::info!("Stale mount cleaned up successfully");
                return;
            }
            std::thread::sleep(std::time::Duration::from_millis(100));
        }
        tracing::warn!("Could not fully clean up stale mount at {:?}", mount_point);
    }
}

impl Drop for FuseController {
    fn drop(&mut self) {
        self.unmount();
    }
}

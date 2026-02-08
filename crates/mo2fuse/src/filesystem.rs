//! fuser::Filesystem implementation for the MO2 virtual filesystem.
//!
//! Implements FUSE operations:
//! - Read path: lookup, getattr, readdir, readdirplus, open, read
//! - Write path: write, create, mkdir, unlink, rename -> overwrite directory
//!
//! Performance features:
//! - Rayon thread pool: handlers dispatch work off the FUSE dispatch thread
//! - Long TTLs: kernel caches metadata for ~1 year (safe since we control all mutations)
//! - READDIRPLUS: single call returns entries with full attributes
//! - Tuned kernel config: max_background=512, 1MB readahead, writeback cache
//! - Partial writes: seek+write instead of read-modify-write
//! - FOPEN_KEEP_CACHE: kernel keeps page cache across open/close

use std::collections::HashMap;
use std::ffi::OsStr;
use std::io::{Read as IoRead, Seek, SeekFrom, Write as IoWrite};
use std::os::raw::c_int;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use fuser::{
    FileAttr, FileType, Filesystem, KernelConfig, ReplyAttr, ReplyCreate, ReplyData,
    ReplyDirectory, ReplyDirectoryPlus, ReplyEmpty, ReplyEntry, ReplyOpen, ReplyWrite, Request,
    TimeOrNow,
};

use crate::inode::InodeTable;
use crate::overlay::{VfsNode, VfsTree};
use crate::overwrite::OverwriteManager;

/// Cache TTL for metadata. Since all mutations go through our FUSE daemon,
/// the kernel automatically invalidates entries on mutating ops (create, unlink, write).
/// A long TTL is safe and eliminates repeated metadata round-trips.
const TTL: Duration = Duration::from_secs(86400 * 365); // ~1 year
const BLOCK_SIZE: u32 = 512;

/// Cached uid/gid to avoid repeated syscalls in attr helpers.
struct UidGid {
    uid: u32,
    gid: u32,
}

impl UidGid {
    fn current() -> Self {
        UidGid {
            uid: unsafe { libc::getuid() },
            gid: unsafe { libc::getgid() },
        }
    }
}

/// The FUSE filesystem for MO2.
///
/// All shared state is wrapped in Arc so handlers can be dispatched
/// to rayon's thread pool, freeing the FUSE dispatch thread immediately.
pub struct Mo2Filesystem {
    /// Shared VFS tree (can be swapped for live rebuild)
    tree: Arc<RwLock<VfsTree>>,
    /// Inode table
    inodes: Arc<Mutex<InodeTable>>,
    /// Write manager (points to staging dir while mounted)
    overwrite: Arc<OverwriteManager>,
    /// Open file handles: fh -> (real_path, writable, relative_vfs_path)
    open_files: Arc<Mutex<HashMap<u64, (PathBuf, bool, String)>>>,
    /// Next file handle (atomic — no mutex needed)
    next_fh: Arc<AtomicU64>,
    /// Origin label for files written through the VFS
    write_origin: Arc<String>,
    /// Cached uid/gid
    ids: Arc<UidGid>,
}

impl Mo2Filesystem {
    pub fn new(tree: Arc<RwLock<VfsTree>>, write_dir: PathBuf) -> Self {
        Mo2Filesystem {
            tree,
            inodes: Arc::new(Mutex::new(InodeTable::new())),
            overwrite: Arc::new(OverwriteManager::new(&write_dir)),
            open_files: Arc::new(Mutex::new(HashMap::new())),
            next_fh: Arc::new(AtomicU64::new(1)),
            write_origin: Arc::new("Staging".to_string()),
            ids: Arc::new(UidGid::current()),
        }
    }

    /// Resolve an inode to its VFS path.
    fn inode_to_path(inodes: &Mutex<InodeTable>, ino: u64) -> Option<String> {
        let inodes = inodes.lock().unwrap();
        inodes.get_path(ino).map(|s| s.to_string())
    }

    /// Resolve a VFS path to its node.
    fn resolve_path<'a>(path: &str, tree: &'a VfsTree) -> Option<&'a VfsNode> {
        if path.is_empty() {
            Some(&tree.root)
        } else {
            tree.root.resolve(path)
        }
    }

    /// Create FileAttr for a directory.
    fn dir_attr(ids: &UidGid, ino: u64) -> FileAttr {
        FileAttr {
            ino,
            size: 0,
            blocks: 0,
            atime: UNIX_EPOCH,
            mtime: UNIX_EPOCH,
            ctime: UNIX_EPOCH,
            crtime: UNIX_EPOCH,
            kind: FileType::Directory,
            perm: 0o755,
            nlink: 2,
            uid: ids.uid,
            gid: ids.gid,
            rdev: 0,
            blksize: BLOCK_SIZE,
            flags: 0,
        }
    }

    /// Create FileAttr for a file.
    fn file_attr(ids: &UidGid, ino: u64, size: u64, mtime: SystemTime) -> FileAttr {
        FileAttr {
            ino,
            size,
            blocks: (size + BLOCK_SIZE as u64 - 1) / BLOCK_SIZE as u64,
            atime: mtime,
            mtime,
            ctime: mtime,
            crtime: mtime,
            kind: FileType::RegularFile,
            perm: 0o644,
            nlink: 1,
            uid: ids.uid,
            gid: ids.gid,
            rdev: 0,
            blksize: BLOCK_SIZE,
            flags: 0,
        }
    }

    /// Remove an entry from the VFS tree by relative path.
    fn remove_from_tree(root: &mut VfsNode, relative: &str) {
        let components: Vec<&str> = relative.split('/').filter(|s| !s.is_empty()).collect();
        if components.is_empty() {
            return;
        }
        Self::remove_node(root, &components);
    }

    fn remove_node(node: &mut VfsNode, components: &[&str]) {
        if components.is_empty() {
            return;
        }

        if let VfsNode::Directory {
            children,
            display_names,
        } = node
        {
            let name = components[0];
            let normalized = mo2core::paths::normalize_for_lookup(name);

            if components.len() == 1 {
                children.remove(&normalized);
                display_names.remove(&normalized);
            } else {
                if let Some(child) = children.get_mut(&normalized) {
                    Self::remove_node(child, &components[1..]);
                }
            }
        }
    }

    /// Allocate a new file handle (lock-free).
    fn alloc_fh(next_fh: &AtomicU64) -> u64 {
        next_fh.fetch_add(1, Ordering::Relaxed)
    }
}

impl Filesystem for Mo2Filesystem {
    fn init(&mut self, _req: &Request, config: &mut KernelConfig) -> Result<(), c_int> {
        // 1MB readahead for large sequential reads (textures, BSAs)
        let _ = config.set_max_readahead(1_048_576);
        // 1MB max write size
        let _ = config.set_max_write(1_048_576);
        // Raise background request limit from default 12 to 512
        let _ = config.set_max_background(512);
        // Congestion threshold at 75% of max_background
        let _ = config.set_congestion_threshold(384);
        // Enable READDIRPLUS (readdir + lookup in one call).
        // Note: FUSE_WRITEBACK_CACHE is intentionally omitted — it delays writes
        // in the kernel page cache, which breaks Wine/Proton's expectation that
        // writes are immediately visible to other file handles.
        let _ = config.add_capabilities(
            fuser::consts::FUSE_DO_READDIRPLUS | fuser::consts::FUSE_READDIRPLUS_AUTO,
        );
        Ok(())
    }

    fn lookup(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: ReplyEntry) {
        let name_str = name.to_string_lossy().to_string();
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let ids = self.ids.clone();

        rayon::spawn(move || {
            let parent_path = match Self::inode_to_path(&inodes, parent) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let child_path = if parent_path.is_empty() {
                name_str
            } else {
                format!("{}/{}", parent_path, name_str)
            };

            let tree = tree.read().unwrap();
            match Self::resolve_path(&child_path, &tree) {
                Some(node) => {
                    let ino = inodes.lock().unwrap().get_or_create(&child_path);
                    let attr = match node {
                        VfsNode::Directory { .. } => Self::dir_attr(&ids, ino),
                        VfsNode::File { size, mtime, .. } => {
                            Self::file_attr(&ids, ino, *size, *mtime)
                        }
                    };
                    reply.entry(&TTL, &attr, 0);
                }
                None => {
                    reply.error(libc::ENOENT);
                }
            }
        });
    }

    fn getattr(&mut self, _req: &Request, ino: u64, _fh: Option<u64>, reply: ReplyAttr) {
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let ids = self.ids.clone();

        rayon::spawn(move || {
            if ino == 1 {
                reply.attr(&TTL, &Self::dir_attr(&ids, 1));
                return;
            }

            let path = match Self::inode_to_path(&inodes, ino) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let tree = tree.read().unwrap();
            match Self::resolve_path(&path, &tree) {
                Some(VfsNode::Directory { .. }) => {
                    reply.attr(&TTL, &Self::dir_attr(&ids, ino));
                }
                Some(VfsNode::File { size, mtime, .. }) => {
                    reply.attr(&TTL, &Self::file_attr(&ids, ino, *size, *mtime));
                }
                None => {
                    reply.error(libc::ENOENT);
                }
            }
        });
    }

    fn readdir(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        mut reply: ReplyDirectory,
    ) {
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();

        rayon::spawn(move || {
            let path = match Self::inode_to_path(&inodes, ino) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let tree = tree.read().unwrap();
            let node = match Self::resolve_path(&path, &tree) {
                Some(n) => n,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            // Batch inode allocation: collect all children, then lock once
            let children: Vec<(String, bool)> = node
                .list_children()
                .into_iter()
                .map(|(name, child): (&str, &VfsNode)| (name.to_string(), child.is_directory()))
                .collect();

            let mut entries: Vec<(u64, FileType, String)> = Vec::with_capacity(children.len() + 2);
            entries.push((ino, FileType::Directory, ".".to_string()));
            entries.push((1, FileType::Directory, "..".to_string()));

            {
                let mut inodes = inodes.lock().unwrap();
                for (name, is_dir) in &children {
                    let child_path = if path.is_empty() {
                        name.clone()
                    } else {
                        format!("{}/{}", path, name)
                    };
                    let child_ino = inodes.get_or_create(&child_path);
                    let file_type = if *is_dir {
                        FileType::Directory
                    } else {
                        FileType::RegularFile
                    };
                    entries.push((child_ino, file_type, name.clone()));
                }
            }

            for (i, (ino, kind, name)) in entries.iter().enumerate().skip(offset as usize) {
                if reply.add(*ino, (i + 1) as i64, *kind, name) {
                    break;
                }
            }

            reply.ok();
        });
    }

    fn readdirplus(
        &mut self,
        _req: &Request,
        ino: u64,
        _fh: u64,
        offset: i64,
        mut reply: ReplyDirectoryPlus,
    ) {
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let ids = self.ids.clone();

        rayon::spawn(move || {
            let path = match Self::inode_to_path(&inodes, ino) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let tree = tree.read().unwrap();
            let node = match Self::resolve_path(&path, &tree) {
                Some(n) => n,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            // Collect children with full attr info
            let children: Vec<(String, bool, u64, SystemTime)> = node
                .list_children()
                .into_iter()
                .map(|(name, child): (&str, &VfsNode)| {
                    let (is_dir, size, mtime) = match child {
                        VfsNode::Directory { .. } => (true, 0u64, UNIX_EPOCH),
                        VfsNode::File { size, mtime, .. } => (false, *size, *mtime),
                    };
                    (name.to_string(), is_dir, size, mtime)
                })
                .collect();

            // . and .. entries
            let dot_attr = Self::dir_attr(&ids, ino);
            let dotdot_attr = Self::dir_attr(&ids, 1);

            let mut idx: i64 = 0;

            if offset <= idx {
                if reply.add(ino, idx + 1, ".", &TTL, &dot_attr, 0) {
                    reply.ok();
                    return;
                }
            }
            idx += 1;

            if offset <= idx {
                if reply.add(1, idx + 1, "..", &TTL, &dotdot_attr, 0) {
                    reply.ok();
                    return;
                }
            }
            idx += 1;

            let mut inodes = inodes.lock().unwrap();
            for (name, is_dir, size, mtime) in &children {
                if offset <= idx {
                    let child_path = if path.is_empty() {
                        name.clone()
                    } else {
                        format!("{}/{}", path, name)
                    };
                    let child_ino = inodes.get_or_create(&child_path);
                    let attr = if *is_dir {
                        Self::dir_attr(&ids, child_ino)
                    } else {
                        Self::file_attr(&ids, child_ino, *size, *mtime)
                    };
                    if reply.add(child_ino, idx + 1, name, &TTL, &attr, 0) {
                        break;
                    }
                }
                idx += 1;
            }

            reply.ok();
        });
    }

    fn open(&mut self, _req: &Request, ino: u64, flags: i32, reply: ReplyOpen) {
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let overwrite = self.overwrite.clone();
        let open_files = self.open_files.clone();
        let next_fh = self.next_fh.clone();
        let write_origin = self.write_origin.clone();

        rayon::spawn(move || {
            let path = match Self::inode_to_path(&inodes, ino) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            // Read the real path, then drop the read lock before potentially taking a write lock
            let real_path = {
                let tree_guard = tree.read().unwrap();
                match Self::resolve_path(&path, &tree_guard) {
                    Some(VfsNode::File { real_path, .. }) => real_path.clone(),
                    _ => {
                        reply.error(libc::ENOENT);
                        return;
                    }
                }
            }; // read lock dropped here

            let writable = (flags & libc::O_WRONLY != 0) || (flags & libc::O_RDWR != 0);

            // If writable, copy-on-write to overwrite directory
            let actual_path = if writable {
                match overwrite.copy_on_write(&real_path, &path) {
                    Ok(p) => {
                        // Update VFS tree to point to the staging copy
                        if let Ok(metadata) = std::fs::metadata(&p) {
                            let mut tree_guard = tree.write().unwrap();
                            let components: Vec<&str> = path.split('/').collect();
                            tree_guard.root.insert_file(
                                &components,
                                p.clone(),
                                metadata.len(),
                                metadata.modified().unwrap_or(UNIX_EPOCH),
                                &write_origin,
                            );
                        }
                        p
                    }
                    Err(_) => {
                        reply.error(libc::EIO);
                        return;
                    }
                }
            } else {
                real_path
            };

            let fh = Self::alloc_fh(&next_fh);
            open_files
                .lock()
                .unwrap()
                .insert(fh, (actual_path, writable, path));

            // FOPEN_KEEP_CACHE: tell kernel to keep page cache across open/close cycles.
            // Safe because we control all mutations through the FUSE daemon.
            reply.opened(fh, fuser::consts::FOPEN_KEEP_CACHE);
        });
    }

    fn read(
        &mut self,
        _req: &Request,
        _ino: u64,
        fh: u64,
        offset: i64,
        size: u32,
        _flags: i32,
        _lock_owner: Option<u64>,
        reply: ReplyData,
    ) {
        let open_files = self.open_files.clone();

        rayon::spawn(move || {
            let real_path = {
                let files = open_files.lock().unwrap();
                match files.get(&fh) {
                    Some((path, _, _)) => path.clone(),
                    None => {
                        reply.error(libc::EBADF);
                        return;
                    }
                }
            };

            match std::fs::File::open(&real_path) {
                Ok(mut file) => {
                    if file.seek(SeekFrom::Start(offset as u64)).is_err() {
                        reply.data(&[]);
                        return;
                    }
                    let mut buf = vec![0u8; size as usize];
                    match file.read(&mut buf) {
                        Ok(n) => reply.data(&buf[..n]),
                        Err(_) => reply.error(libc::EIO),
                    }
                }
                Err(_) => reply.error(libc::EIO),
            }
        });
    }

    fn write(
        &mut self,
        _req: &Request,
        _ino: u64,
        fh: u64,
        offset: i64,
        data: &[u8],
        _write_flags: u32,
        _flags: i32,
        _lock_owner: Option<u64>,
        reply: ReplyWrite,
    ) {
        // Copy data into owned buffer so we can move it into rayon::spawn
        let data = data.to_vec();
        let open_files = self.open_files.clone();
        let tree = self.tree.clone();
        let write_origin = self.write_origin.clone();

        rayon::spawn(move || {
            let (real_path, writable, relative_path) = {
                let files = open_files.lock().unwrap();
                match files.get(&fh) {
                    Some((p, w, rel)) => (p.clone(), *w, rel.clone()),
                    None => {
                        reply.error(libc::EBADF);
                        return;
                    }
                }
            };

            if !writable {
                reply.error(libc::EACCES);
                return;
            }

            // Partial write: open file, seek to offset, write only the data chunk.
            // This replaces the old read-modify-write pattern that read the entire file.
            match std::fs::OpenOptions::new()
                .write(true)
                .create(true)
                .open(&real_path)
            {
                Ok(mut file) => {
                    if file.seek(SeekFrom::Start(offset as u64)).is_err() {
                        reply.error(libc::EIO);
                        return;
                    }
                    match file.write_all(&data) {
                        Ok(()) => {
                            // Keep VFS metadata (size/mtime) in sync with on-disk writes.
                            if let Ok(metadata) = std::fs::metadata(&real_path) {
                                let mut tree_guard = tree.write().unwrap();
                                let components: Vec<&str> = relative_path.split('/').collect();
                                tree_guard.root.insert_file(
                                    &components,
                                    real_path.clone(),
                                    metadata.len(),
                                    metadata.modified().unwrap_or(UNIX_EPOCH),
                                    &write_origin,
                                );
                            }
                            reply.written(data.len() as u32)
                        }
                        Err(_) => reply.error(libc::EIO),
                    }
                }
                Err(_) => reply.error(libc::EIO),
            }
        });
    }

    fn create(
        &mut self,
        _req: &Request,
        parent: u64,
        name: &OsStr,
        _mode: u32,
        _umask: u32,
        _flags: i32,
        reply: ReplyCreate,
    ) {
        let name_str = name.to_string_lossy().to_string();
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let overwrite = self.overwrite.clone();
        let open_files = self.open_files.clone();
        let next_fh = self.next_fh.clone();
        let write_origin = self.write_origin.clone();
        let ids = self.ids.clone();

        rayon::spawn(move || {
            let parent_path = match Self::inode_to_path(&inodes, parent) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let relative = if parent_path.is_empty() {
                name_str
            } else {
                format!("{}/{}", parent_path, name_str)
            };

            match overwrite.write_file(&relative, &[]) {
                Ok(real_path) => {
                    {
                        let mut tree_guard = tree.write().unwrap();
                        let components: Vec<&str> = relative.split('/').collect();
                        tree_guard.root.insert_file(
                            &components,
                            real_path.clone(),
                            0,
                            SystemTime::now(),
                            &write_origin,
                        );
                        tree_guard.file_count += 1;
                    }

                    let ino = inodes.lock().unwrap().get_or_create(&relative);
                    let attr = Self::file_attr(&ids, ino, 0, SystemTime::now());
                    let fh = Self::alloc_fh(&next_fh);
                    open_files
                        .lock()
                        .unwrap()
                        .insert(fh, (real_path, true, relative));
                    reply.created(&TTL, &attr, 0, fh, 0);
                }
                Err(_) => {
                    reply.error(libc::EIO);
                }
            }
        });
    }

    fn mkdir(
        &mut self,
        _req: &Request,
        parent: u64,
        name: &OsStr,
        _mode: u32,
        _umask: u32,
        reply: ReplyEntry,
    ) {
        let name_str = name.to_string_lossy().to_string();
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let overwrite = self.overwrite.clone();
        let ids = self.ids.clone();

        rayon::spawn(move || {
            let parent_path = match Self::inode_to_path(&inodes, parent) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let relative = if parent_path.is_empty() {
                name_str
            } else {
                format!("{}/{}", parent_path, name_str)
            };

            match overwrite.create_dir(&relative) {
                Ok(_) => {
                    {
                        let mut tree_guard = tree.write().unwrap();
                        let components: Vec<&str> = relative.split('/').collect();
                        tree_guard.root.insert_directory(&components);
                        tree_guard.dir_count += 1;
                    }

                    let ino = inodes.lock().unwrap().get_or_create(&relative);
                    reply.entry(&TTL, &Self::dir_attr(&ids, ino), 0);
                }
                Err(_) => {
                    reply.error(libc::EIO);
                }
            }
        });
    }

    fn unlink(&mut self, _req: &Request, parent: u64, name: &OsStr, reply: ReplyEmpty) {
        let name_str = name.to_string_lossy().to_string();
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let overwrite = self.overwrite.clone();

        rayon::spawn(move || {
            let parent_path = match Self::inode_to_path(&inodes, parent) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let relative = if parent_path.is_empty() {
                name_str
            } else {
                format!("{}/{}", parent_path, name_str)
            };

            if overwrite.exists(&relative) {
                match overwrite.remove_file(&relative) {
                    Ok(()) => {
                        let mut tree_guard = tree.write().unwrap();
                        Self::remove_from_tree(&mut tree_guard.root, &relative);
                        tree_guard.file_count = tree_guard.file_count.saturating_sub(1);
                        reply.ok()
                    }
                    Err(_) => reply.error(libc::EIO),
                }
            } else {
                reply.error(libc::EACCES);
            }
        });
    }

    fn rename(
        &mut self,
        _req: &Request,
        parent: u64,
        name: &OsStr,
        newparent: u64,
        newname: &OsStr,
        _flags: u32,
        reply: ReplyEmpty,
    ) {
        let name_str = name.to_string_lossy().to_string();
        let new_name_str = newname.to_string_lossy().to_string();
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let overwrite = self.overwrite.clone();
        let write_origin = self.write_origin.clone();

        rayon::spawn(move || {
            let parent_path = match Self::inode_to_path(&inodes, parent) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let new_parent_path = match Self::inode_to_path(&inodes, newparent) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            let old_relative = if parent_path.is_empty() {
                name_str
            } else {
                format!("{}/{}", parent_path, name_str)
            };

            let new_relative = if new_parent_path.is_empty() {
                new_name_str
            } else {
                format!("{}/{}", new_parent_path, new_name_str)
            };

            if overwrite.exists(&old_relative) {
                match overwrite.rename(&old_relative, &new_relative) {
                    Ok(()) => {
                        let new_real_path = overwrite.overwrite_path(&new_relative);
                        let (size, mtime) = std::fs::metadata(&new_real_path)
                            .map(|m| (m.len(), m.modified().unwrap_or(UNIX_EPOCH)))
                            .unwrap_or((0, UNIX_EPOCH));

                        let mut tree_guard = tree.write().unwrap();
                        Self::remove_from_tree(&mut tree_guard.root, &old_relative);
                        let components: Vec<&str> = new_relative.split('/').collect();
                        tree_guard.root.insert_file(
                            &components,
                            new_real_path,
                            size,
                            mtime,
                            &write_origin,
                        );

                        let mut inodes = inodes.lock().unwrap();
                        inodes.rename(&old_relative, &new_relative);

                        reply.ok()
                    }
                    Err(_) => reply.error(libc::EIO),
                }
            } else {
                reply.error(libc::EACCES);
            }
        });
    }

    fn release(
        &mut self,
        _req: &Request,
        _ino: u64,
        fh: u64,
        _flags: i32,
        _lock_owner: Option<u64>,
        _flush: bool,
        reply: ReplyEmpty,
    ) {
        let open_files = self.open_files.clone();

        rayon::spawn(move || {
            open_files.lock().unwrap().remove(&fh);
            reply.ok();
        });
    }

    fn setattr(
        &mut self,
        _req: &Request,
        ino: u64,
        _mode: Option<u32>,
        _uid: Option<u32>,
        _gid: Option<u32>,
        size: Option<u64>,
        _atime: Option<TimeOrNow>,
        _mtime: Option<TimeOrNow>,
        _ctime: Option<SystemTime>,
        fh: Option<u64>,
        _crtime: Option<SystemTime>,
        _chgtime: Option<SystemTime>,
        _bkuptime: Option<SystemTime>,
        _flags: Option<u32>,
        reply: ReplyAttr,
    ) {
        let inodes = self.inodes.clone();
        let tree = self.tree.clone();
        let overwrite = self.overwrite.clone();
        let open_files = self.open_files.clone();
        let write_origin = self.write_origin.clone();
        let ids = self.ids.clone();

        rayon::spawn(move || {
            if ino == 1 {
                reply.attr(&TTL, &Self::dir_attr(&ids, 1));
                return;
            }

            let path = match Self::inode_to_path(&inodes, ino) {
                Some(p) => p,
                None => {
                    reply.error(libc::ENOENT);
                    return;
                }
            };

            // Handle truncate (size change)
            if let Some(new_size) = size {
                let real_path = if let Some(fh_val) = fh {
                    let files = open_files.lock().unwrap();
                    files.get(&fh_val).map(|(p, _, _)| p.clone())
                } else {
                    let ow_path = overwrite.overwrite_path(&path);
                    if ow_path.exists() {
                        Some(ow_path)
                    } else {
                        let tree_guard = tree.read().unwrap();
                        match Self::resolve_path(&path, &tree_guard) {
                            Some(VfsNode::File { real_path, .. }) => Some(real_path.clone()),
                            _ => None,
                        }
                    }
                };

                if let Some(rp) = real_path {
                    let target = if !rp.starts_with(&overwrite.overwrite_dir) {
                        match overwrite.copy_on_write(&rp, &path) {
                            Ok(p) => p,
                            Err(_) => {
                                reply.error(libc::EIO);
                                return;
                            }
                        }
                    } else {
                        rp
                    };

                    if let Ok(f) = std::fs::OpenOptions::new().write(true).open(&target) {
                        let _ = f.set_len(new_size);
                    }

                    {
                        let mut tree_guard = tree.write().unwrap();
                        let components: Vec<&str> = path.split('/').collect();
                        tree_guard.root.insert_file(
                            &components,
                            target,
                            new_size,
                            SystemTime::now(),
                            &write_origin,
                        );
                    }

                    reply.attr(
                        &TTL,
                        &Self::file_attr(&ids, ino, new_size, SystemTime::now()),
                    );
                    return;
                }
            }

            let tree_guard = tree.read().unwrap();
            match Self::resolve_path(&path, &tree_guard) {
                Some(VfsNode::Directory { .. }) => {
                    reply.attr(&TTL, &Self::dir_attr(&ids, ino));
                }
                Some(VfsNode::File { size, mtime, .. }) => {
                    reply.attr(&TTL, &Self::file_attr(&ids, ino, *size, *mtime));
                }
                None => {
                    reply.error(libc::ENOENT);
                }
            }
        });
    }
}

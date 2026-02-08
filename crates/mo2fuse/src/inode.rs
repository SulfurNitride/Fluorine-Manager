//! Inode table for the FUSE filesystem.
//!
//! Maps VFS paths to inode numbers and back.
//! Inode 1 = root directory (FUSE convention).

use std::collections::HashMap;

/// Manages inode allocation and lookup.
#[derive(Debug)]
pub struct InodeTable {
    /// Path -> inode number
    path_to_inode: HashMap<String, u64>,
    /// Inode number -> path
    inode_to_path: HashMap<u64, String>,
    /// Next inode to allocate
    next_inode: u64,
}

impl InodeTable {
    /// Create a new inode table with root (inode 1) pre-allocated.
    pub fn new() -> Self {
        let mut table = InodeTable {
            path_to_inode: HashMap::new(),
            inode_to_path: HashMap::new(),
            next_inode: 2, // 1 is reserved for root
        };
        table.path_to_inode.insert(String::new(), 1);
        table.inode_to_path.insert(1, String::new());
        table
    }

    /// Get or allocate an inode for a path.
    pub fn get_or_create(&mut self, path: &str) -> u64 {
        let key = normalize_key(path);
        if let Some(&ino) = self.path_to_inode.get(&key) {
            return ino;
        }

        let ino = self.next_inode;
        self.next_inode += 1;
        self.path_to_inode.insert(key, ino);
        self.inode_to_path.insert(ino, canonicalize_path(path));
        ino
    }

    /// Look up an inode by path.
    pub fn get_inode(&self, path: &str) -> Option<u64> {
        self.path_to_inode.get(&normalize_key(path)).copied()
    }

    /// Look up a path by inode.
    pub fn get_path(&self, inode: u64) -> Option<&str> {
        self.inode_to_path.get(&inode).map(|s| s.as_str())
    }

    /// Get the root inode (always 1).
    pub fn root_inode(&self) -> u64 {
        1
    }

    /// Total number of allocated inodes.
    pub fn count(&self) -> usize {
        self.path_to_inode.len()
    }

    /// Rename an inode entry (update path mapping, keep same inode number).
    pub fn rename(&mut self, old_path: &str, new_path: &str) {
        let old_key = normalize_key(old_path);
        let new_key = normalize_key(new_path);
        if let Some(ino) = self.path_to_inode.remove(&old_key) {
            self.inode_to_path.insert(ino, canonicalize_path(new_path));
            self.path_to_inode.insert(new_key, ino);
        }
    }

    /// Clear all entries except root.
    pub fn clear(&mut self) {
        self.path_to_inode.clear();
        self.inode_to_path.clear();
        self.next_inode = 2;
        self.path_to_inode.insert(String::new(), 1);
        self.inode_to_path.insert(1, String::new());
    }
}

impl Default for InodeTable {
    fn default() -> Self {
        Self::new()
    }
}

/// Normalize a path for case-insensitive inode lookup.
fn normalize_key(path: &str) -> String {
    path.to_lowercase()
        .replace('\\', "/")
        .trim_matches('/')
        .to_string()
}

/// Canonicalize a path while preserving case.
fn canonicalize_path(path: &str) -> String {
    path.replace('\\', "/").trim_matches('/').to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_root_inode() {
        let table = InodeTable::new();
        assert_eq!(table.root_inode(), 1);
        assert_eq!(table.get_inode(""), Some(1));
        assert_eq!(table.get_path(1), Some(""));
    }

    #[test]
    fn test_allocate() {
        let mut table = InodeTable::new();
        let ino = table.get_or_create("Textures/Armor.dds");
        assert_eq!(ino, 2);
        assert_eq!(table.get_inode("textures/armor.dds"), Some(2));
        assert_eq!(table.get_path(2), Some("Textures/Armor.dds"));
    }

    #[test]
    fn test_dedup() {
        let mut table = InodeTable::new();
        let ino1 = table.get_or_create("textures/armor.dds");
        let ino2 = table.get_or_create("textures/armor.dds");
        assert_eq!(ino1, ino2);
    }

    #[test]
    fn test_case_insensitive() {
        let mut table = InodeTable::new();
        let ino1 = table.get_or_create("Textures/Armor.dds");
        let ino2 = table.get_or_create("textures/armor.dds");
        assert_eq!(ino1, ino2);
        assert_eq!(table.get_path(ino1), Some("Textures/Armor.dds"));
    }

    #[test]
    fn test_rename_preserves_case() {
        let mut table = InodeTable::new();
        let ino = table.get_or_create("Data/SKSE/Plugins/Foo.ini");
        table.rename("data/skse/plugins/foo.ini", "Data/SKSE/Plugins/Bar.ini");
        assert_eq!(table.get_inode("data/skse/plugins/bar.ini"), Some(ino));
        assert_eq!(table.get_path(ino), Some("Data/SKSE/Plugins/Bar.ini"));
    }

    #[test]
    fn test_clear() {
        let mut table = InodeTable::new();
        table.get_or_create("test.txt");
        assert_eq!(table.count(), 2); // root + test.txt
        table.clear();
        assert_eq!(table.count(), 1); // just root
    }
}

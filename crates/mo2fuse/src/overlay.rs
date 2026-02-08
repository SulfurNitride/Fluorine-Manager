//! VFS tree builder - merges mod directories by priority.
//!
//! Builds an in-memory tree representing the merged virtual filesystem.
//! Higher-priority mods win file conflicts (same path).
//! Directories are merged across all mods.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use mo2core::paths::normalize_for_lookup;

/// A node in the virtual filesystem tree.
#[derive(Debug, Clone)]
pub enum VfsNode {
    /// A directory containing child entries.
    Directory {
        /// Children by normalized name
        children: HashMap<String, VfsNode>,
        /// Preferred display name by normalized key (preserving case)
        display_names: HashMap<String, String>,
    },
    /// A file backed by a real file on disk.
    File {
        /// Path to the actual file on disk
        real_path: PathBuf,
        /// File size in bytes
        size: u64,
        /// Modification time
        mtime: SystemTime,
        /// Which mod provides this file
        origin: String,
    },
}

impl VfsNode {
    /// Create a new empty directory node.
    pub fn new_directory() -> Self {
        VfsNode::Directory {
            children: HashMap::new(),
            display_names: HashMap::new(),
        }
    }

    /// Check if this node is a directory.
    pub fn is_directory(&self) -> bool {
        matches!(self, VfsNode::Directory { .. })
    }

    /// Check if this node is a file.
    pub fn is_file(&self) -> bool {
        matches!(self, VfsNode::File { .. })
    }

    /// Get child by normalized name (for directories).
    pub fn get_child(&self, name: &str) -> Option<&VfsNode> {
        match self {
            VfsNode::Directory { children, .. } => children.get(&normalize_for_lookup(name)),
            _ => None,
        }
    }

    /// List children (for directories). Returns (display_name, node) pairs.
    pub fn list_children(&self) -> Vec<(&str, &VfsNode)> {
        match self {
            VfsNode::Directory {
                children,
                display_names,
            } => display_names
                .iter()
                .filter_map(|(normalized, display)| {
                    children
                        .get(normalized)
                        .map(|node| (display.as_str(), node))
                })
                .collect(),
            _ => Vec::new(),
        }
    }

    /// Resolve a path through the tree. Returns the node at the given path.
    pub fn resolve(&self, path: &str) -> Option<&VfsNode> {
        let components: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();

        let mut current = self;
        for component in components {
            current = current.get_child(component)?;
        }
        Some(current)
    }

    /// Insert an empty directory into the tree, creating intermediate directories as needed.
    pub fn insert_directory(&mut self, relative_path: &[&str]) {
        if relative_path.is_empty() {
            return;
        }

        if let VfsNode::Directory {
            children,
            display_names,
        } = self
        {
            let component = relative_path[0];
            let normalized = normalize_for_lookup(component);

            let child = children
                .entry(normalized.clone())
                .or_insert_with(VfsNode::new_directory);

            if !display_names.contains_key(&normalized) {
                display_names.insert(normalized.clone(), component.to_string());
            }

            if relative_path.len() > 1 {
                child.insert_directory(&relative_path[1..]);
            }
        }
    }

    /// Insert a file into the tree, creating intermediate directories as needed.
    pub fn insert_file(
        &mut self,
        relative_path: &[&str],
        real_path: PathBuf,
        size: u64,
        mtime: SystemTime,
        origin: &str,
    ) {
        if relative_path.is_empty() {
            return;
        }

        if let VfsNode::Directory {
            children,
            display_names,
        } = self
        {
            let component = relative_path[0];
            let normalized = normalize_for_lookup(component);

            if relative_path.len() == 1 {
                // This is the file itself
                let file_node = VfsNode::File {
                    real_path,
                    size,
                    mtime,
                    origin: origin.to_string(),
                };
                children.insert(normalized.clone(), file_node);
                display_names.insert(normalized, component.to_string());
            } else {
                // Intermediate directory
                let child = children
                    .entry(normalized.clone())
                    .or_insert_with(VfsNode::new_directory);

                if !display_names.contains_key(&normalized) {
                    display_names.insert(normalized.clone(), component.to_string());
                }

                child.insert_file(&relative_path[1..], real_path, size, mtime, origin);
            }
        }
    }
}

/// The complete VFS tree.
#[derive(Debug, Clone)]
pub struct VfsTree {
    pub root: VfsNode,
    /// Total file count
    pub file_count: usize,
    /// Total directory count
    pub dir_count: usize,
}

/// A flattened entry for display in the Data tab.
#[derive(Debug, Clone)]
pub struct VfsDisplayEntry {
    /// Display path relative to root
    pub name: String,
    /// Full virtual path
    pub virtual_path: String,
    /// Which mod provides this file (empty for directories)
    pub origin: String,
    /// Whether this is a directory
    pub is_directory: bool,
    /// Depth in the tree (0 = root children)
    pub depth: u32,
    /// File size (0 for directories)
    pub size: u64,
}

impl VfsTree {
    pub fn new() -> Self {
        VfsTree {
            root: VfsNode::new_directory(),
            file_count: 0,
            dir_count: 1, // Root directory
        }
    }

    /// Flatten the tree into a list for display, respecting expanded state.
    /// `expanded_paths` is the set of virtual paths that are expanded.
    pub fn flatten_for_display(
        &self,
        expanded_paths: &std::collections::HashSet<String>,
    ) -> Vec<VfsDisplayEntry> {
        let mut entries = Vec::new();
        Self::flatten_node(&self.root, "", 0, expanded_paths, &mut entries);
        entries
    }

    fn flatten_node(
        node: &VfsNode,
        parent_path: &str,
        depth: u32,
        expanded_paths: &std::collections::HashSet<String>,
        entries: &mut Vec<VfsDisplayEntry>,
    ) {
        if let VfsNode::Directory { .. } = node {
            let mut children: Vec<(&str, &VfsNode)> = node.list_children();
            // Sort: directories first, then alphabetical
            children.sort_by(|a, b| {
                let a_dir = a.1.is_directory();
                let b_dir = b.1.is_directory();
                b_dir
                    .cmp(&a_dir)
                    .then(a.0.to_lowercase().cmp(&b.0.to_lowercase()))
            });

            for (name, child) in children {
                let virtual_path = if parent_path.is_empty() {
                    name.to_string()
                } else {
                    format!("{}/{}", parent_path, name)
                };

                match child {
                    VfsNode::Directory { .. } => {
                        entries.push(VfsDisplayEntry {
                            name: name.to_string(),
                            virtual_path: virtual_path.clone(),
                            origin: String::new(),
                            is_directory: true,
                            depth,
                            size: 0,
                        });
                        // Recurse if expanded
                        if expanded_paths.contains(&virtual_path) {
                            Self::flatten_node(
                                child,
                                &virtual_path,
                                depth + 1,
                                expanded_paths,
                                entries,
                            );
                        }
                    }
                    VfsNode::File { origin, size, .. } => {
                        entries.push(VfsDisplayEntry {
                            name: name.to_string(),
                            virtual_path,
                            origin: origin.clone(),
                            is_directory: false,
                            depth,
                            size: *size,
                        });
                    }
                }
            }
        }
    }
}

impl Default for VfsTree {
    fn default() -> Self {
        Self::new()
    }
}

/// Build a VFS tree from a list of active mods.
///
/// `mods` is sorted by ascending priority (last entry wins conflicts).
/// `overwrite_dir` is applied first so active mods can override it.
pub fn build_vfs_tree(mods: &[(&str, &Path)], overwrite_dir: &Path) -> anyhow::Result<VfsTree> {
    let mut tree = VfsTree::new();
    let mut file_count = 0usize;
    let mut dir_count = 1usize; // Root

    // Apply overwrite directory first (lower than active mods).
    if overwrite_dir.exists() {
        for entry in walkdir::WalkDir::new(overwrite_dir)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            let relative = match path.strip_prefix(overwrite_dir) {
                Ok(r) => r,
                Err(_) => continue,
            };

            let relative_str = relative.to_string_lossy();
            if relative_str.is_empty() {
                continue;
            }

            if entry.file_type().is_file() {
                let metadata = entry.metadata()?;
                let components: Vec<&str> = relative_str.split('/').collect();

                tree.root.insert_file(
                    &components,
                    path.to_path_buf(),
                    metadata.len(),
                    metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH),
                    "Overwrite",
                );
                file_count += 1;
            } else if entry.file_type().is_dir() {
                dir_count += 1;
            }
        }
    }

    // Process mods in priority order (lowest first, so higher priority overwrites)
    for (mod_name, mod_path) in mods {
        if !mod_path.exists() {
            continue;
        }

        for entry in walkdir::WalkDir::new(mod_path)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            let relative = match path.strip_prefix(mod_path) {
                Ok(r) => r,
                Err(_) => continue,
            };

            // Skip meta.ini and empty relative paths
            let relative_str = relative.to_string_lossy();
            if relative_str.is_empty() || relative_str == "meta.ini" {
                continue;
            }

            if entry.file_type().is_file() {
                let metadata = entry.metadata()?;
                let components: Vec<&str> = relative_str.split('/').collect();

                tree.root.insert_file(
                    &components,
                    path.to_path_buf(),
                    metadata.len(),
                    metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH),
                    mod_name,
                );
                file_count += 1;
            } else if entry.file_type().is_dir() {
                dir_count += 1;
            }
        }
    }

    tree.file_count = file_count;
    tree.dir_count = dir_count;

    Ok(tree)
}

/// Build a full-game VFS tree that merges the entire game directory with mods.
///
/// Unlike `build_vfs_tree` (which only merges Data-level content), this creates
/// a VFS of the **entire game folder**:
/// - Game directory → VFS root (base layer, all files including .exe)
/// - Overwrite directory → lower-priority mutable layer
/// - Each mod's files → placed under `{data_dir_name}/` (e.g., "Data/")
/// - Each mod's `Root/` folder → placed at VFS root (SKSE, ENB, engine fixes)
/// - Active mods → highest priority
///
/// The real game directory is NEVER modified. The VFS mount IS the merged view.
///
/// # Arguments
/// - `game_dir` — the real game installation directory (base layer)
/// - `data_dir_name` — name of the data subdirectory ("Data", "Data Files", etc.)
/// - `mods` — list of (mod_name, mod_path) sorted by ascending priority
/// - `overwrite_dir` — overwrite directory (lower than active mods)
pub fn build_full_game_vfs(
    game_dir: &Path,
    data_dir_name: &str,
    mods: &[(&str, &Path)],
    overwrite_dir: &Path,
) -> anyhow::Result<VfsTree> {
    let mut tree = VfsTree::new();
    let mut file_count = 0usize;
    let mut dir_count = 1usize; // Root

    // Layer 1: The entire game directory as the base (lowest priority)
    if game_dir.exists() {
        add_directory_to_tree(
            &mut tree.root,
            game_dir,
            game_dir,
            "_base_game",
            &[], // no prefix — maps directly to VFS root
            &mut file_count,
            &mut dir_count,
        )?;
    }

    // Layer 2: Overwrite directory (lower than active mods)
    // Overwrite mirrors the VFS structure: Data/ content + root-level content
    if overwrite_dir.exists() {
        add_directory_to_tree(
            &mut tree.root,
            overwrite_dir,
            overwrite_dir,
            "Overwrite",
            &[], // overwrite maps directly to VFS root (it mirrors the game structure)
            &mut file_count,
            &mut dir_count,
        )?;
    }

    // Layer 3: Mods in priority order (lowest first, higher overwrites)
    let data_prefix: Vec<&str> = data_dir_name.split('/').collect();

    for (mod_name, mod_path) in mods {
        if !mod_path.exists() {
            continue;
        }

        let root_dir = mod_path.join("Root");
        let has_root = root_dir.is_dir();
        // Check if mod has a Data/ (or data_dir_name) subfolder alongside Root/
        // If so, the mod structure mirrors the game root — no extra prefix needed
        let has_data_dir = has_root && mod_path.join(data_dir_name).is_dir();

        // Walk the mod directory
        for entry in walkdir::WalkDir::new(mod_path)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            let relative = match path.strip_prefix(mod_path) {
                Ok(r) => r,
                Err(_) => continue,
            };

            let relative_str = relative.to_string_lossy();
            if relative_str.is_empty() || relative_str == "meta.ini" {
                continue;
            }

            // Determine if this file is inside Root/ or regular mod content
            let vfs_components: Vec<String>;

            if has_root {
                if let Ok(root_relative) = path.strip_prefix(&root_dir) {
                    // File is inside Root/ → maps to VFS root (no Data/ prefix)
                    let root_rel_str = root_relative.to_string_lossy();
                    if root_rel_str.is_empty() {
                        continue;
                    }
                    vfs_components = root_rel_str.split('/').map(|s| s.to_string()).collect();
                } else if relative_str.starts_with("Root/")
                    || relative_str.starts_with("Root\\")
                    || relative_str == "Root"
                {
                    // Skip the Root directory entry itself (we handle its contents above)
                    continue;
                } else if has_data_dir {
                    // Mod has both Root/ and Data/ — mod structure mirrors game root.
                    // Files keep their relative path as-is (Data/Scripts/... stays Data/Scripts/...)
                    let rel_parts: Vec<&str> = relative_str.split('/').collect();
                    vfs_components = rel_parts.iter().map(|s| s.to_string()).collect();
                } else {
                    // Root/ exists but no Data/ subfolder — regular mod files go under Data/
                    let rel_parts: Vec<&str> = relative_str.split('/').collect();
                    vfs_components = data_prefix
                        .iter()
                        .chain(rel_parts.iter())
                        .map(|s| s.to_string())
                        .collect();
                }
            } else {
                // No Root/ folder — all files go under Data/
                let rel_parts: Vec<&str> = relative_str.split('/').collect();
                vfs_components = data_prefix
                    .iter()
                    .chain(rel_parts.iter())
                    .map(|s| s.to_string())
                    .collect();
            }

            let components_ref: Vec<&str> = vfs_components.iter().map(|s| s.as_str()).collect();

            if entry.file_type().is_file() {
                let metadata = entry.metadata()?;
                tree.root.insert_file(
                    &components_ref,
                    path.to_path_buf(),
                    metadata.len(),
                    metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH),
                    mod_name,
                );
                file_count += 1;
            } else if entry.file_type().is_dir() {
                dir_count += 1;
            }
        }
    }

    tree.file_count = file_count;
    tree.dir_count = dir_count;

    Ok(tree)
}

/// Helper: add all files from a directory into the VFS tree with an optional path prefix.
fn add_directory_to_tree(
    root: &mut VfsNode,
    walk_dir: &Path,
    strip_prefix: &Path,
    origin: &str,
    prefix: &[&str],
    file_count: &mut usize,
    dir_count: &mut usize,
) -> anyhow::Result<()> {
    for entry in walkdir::WalkDir::new(walk_dir)
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let path = entry.path();
        let relative = match path.strip_prefix(strip_prefix) {
            Ok(r) => r,
            Err(_) => continue,
        };

        let relative_str = relative.to_string_lossy();
        if relative_str.is_empty() {
            continue;
        }

        let rel_parts: Vec<&str> = relative_str.split('/').collect();
        let components: Vec<&str> = prefix
            .iter()
            .copied()
            .chain(rel_parts.iter().copied())
            .collect();

        if entry.file_type().is_file() {
            let metadata = entry.metadata()?;
            root.insert_file(
                &components,
                path.to_path_buf(),
                metadata.len(),
                metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH),
                origin,
            );
            *file_count += 1;
        } else if entry.file_type().is_dir() {
            *dir_count += 1;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_empty_tree() {
        let tree = VfsTree::new();
        assert!(tree.root.is_directory());
        assert_eq!(tree.file_count, 0);
    }

    #[test]
    fn test_build_single_mod() {
        let tmp = tempfile::tempdir().unwrap();
        let mod_dir = tmp.path().join("TestMod");
        std::fs::create_dir_all(mod_dir.join("textures")).unwrap();
        std::fs::write(mod_dir.join("textures/test.dds"), "texture data").unwrap();
        std::fs::write(mod_dir.join("test.esp"), "plugin data").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        let mods = vec![("TestMod", mod_dir.as_path())];
        let tree = build_vfs_tree(&mods, &overwrite).unwrap();

        assert!(tree.file_count >= 2);

        // Verify files are accessible
        let esp = tree.root.resolve("test.esp").unwrap();
        assert!(esp.is_file());

        let texture = tree.root.resolve("textures/test.dds").unwrap();
        assert!(texture.is_file());
    }

    #[test]
    fn test_priority_override() {
        let tmp = tempfile::tempdir().unwrap();

        let mod_a = tmp.path().join("ModA");
        let mod_b = tmp.path().join("ModB");
        std::fs::create_dir_all(mod_a.join("textures")).unwrap();
        std::fs::create_dir_all(mod_b.join("textures")).unwrap();
        std::fs::write(mod_a.join("textures/shared.dds"), "A version").unwrap();
        std::fs::write(mod_b.join("textures/shared.dds"), "B version").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        // ModB has higher priority (last in list)
        let mods = vec![("ModA", mod_a.as_path()), ("ModB", mod_b.as_path())];
        let tree = build_vfs_tree(&mods, &overwrite).unwrap();

        let node = tree.root.resolve("textures/shared.dds").unwrap();
        if let VfsNode::File {
            origin, real_path, ..
        } = node
        {
            assert_eq!(origin, "ModB");
            assert!(real_path.starts_with(&mod_b));
        } else {
            panic!("Expected file node");
        }
    }

    #[test]
    fn test_directory_merging() {
        let tmp = tempfile::tempdir().unwrap();

        let mod_a = tmp.path().join("ModA");
        let mod_b = tmp.path().join("ModB");
        std::fs::create_dir_all(mod_a.join("textures")).unwrap();
        std::fs::create_dir_all(mod_b.join("textures")).unwrap();
        std::fs::write(mod_a.join("textures/a.dds"), "A").unwrap();
        std::fs::write(mod_b.join("textures/b.dds"), "B").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        let mods = vec![("ModA", mod_a.as_path()), ("ModB", mod_b.as_path())];
        let tree = build_vfs_tree(&mods, &overwrite).unwrap();

        // Both files should be visible in the merged textures directory
        assert!(tree.root.resolve("textures/a.dds").is_some());
        assert!(tree.root.resolve("textures/b.dds").is_some());
    }

    #[test]
    fn test_mod_priority_over_overwrite() {
        let tmp = tempfile::tempdir().unwrap();

        let mod_a = tmp.path().join("ModA");
        std::fs::create_dir_all(&mod_a).unwrap();
        std::fs::write(mod_a.join("test.esp"), "mod version").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();
        std::fs::write(overwrite.join("test.esp"), "overwrite version").unwrap();

        let mods = vec![("ModA", mod_a.as_path())];
        let tree = build_vfs_tree(&mods, &overwrite).unwrap();

        let node = tree.root.resolve("test.esp").unwrap();
        if let VfsNode::File { origin, .. } = node {
            assert_eq!(origin, "ModA");
        } else {
            panic!("Expected file node");
        }
    }

    // ── Tests for build_full_game_vfs ──────────────────────────────

    /// Helper: create a fake game directory
    fn make_game_dir(tmp: &Path) -> PathBuf {
        let game_dir = tmp.join("SkyrimSE");
        std::fs::create_dir_all(game_dir.join("Data/textures")).unwrap();
        std::fs::write(game_dir.join("SkyrimSE.exe"), "game binary").unwrap();
        std::fs::write(game_dir.join("Data/Skyrim.esm"), "master file").unwrap();
        std::fs::write(game_dir.join("Data/textures/vanilla.dds"), "vanilla tex").unwrap();
        game_dir
    }

    #[test]
    fn test_full_game_vfs_base_layer() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        let tree = build_full_game_vfs(&game_dir, "Data", &[], &overwrite).unwrap();

        // Game exe should be at VFS root
        assert!(tree.root.resolve("SkyrimSE.exe").is_some());
        // Game data should be under Data/
        assert!(tree.root.resolve("Data/Skyrim.esm").is_some());
        assert!(tree.root.resolve("Data/textures/vanilla.dds").is_some());
    }

    #[test]
    fn test_full_game_vfs_mod_goes_under_data() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        // Create a mod with textures + plugin (no Root/ folder)
        let mod_a = tmp.path().join("TextureMod");
        std::fs::create_dir_all(mod_a.join("textures")).unwrap();
        std::fs::write(mod_a.join("textures/modded.dds"), "mod texture").unwrap();
        std::fs::write(mod_a.join("mod.esp"), "mod plugin").unwrap();

        let mods = vec![("TextureMod", mod_a.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data", &mods, &overwrite).unwrap();

        // Mod files should appear under Data/
        assert!(tree.root.resolve("Data/textures/modded.dds").is_some());
        assert!(tree.root.resolve("Data/mod.esp").is_some());
        // Game exe still at root
        assert!(tree.root.resolve("SkyrimSE.exe").is_some());
        // Vanilla files still present
        assert!(tree.root.resolve("Data/Skyrim.esm").is_some());
    }

    #[test]
    fn test_full_game_vfs_root_folder_at_game_root() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        // Create a mod with Root/ (SKSE) + regular data files
        let mod_skse = tmp.path().join("SKSE");
        std::fs::create_dir_all(mod_skse.join("Root")).unwrap();
        std::fs::write(mod_skse.join("Root/skse64_loader.exe"), "skse loader").unwrap();
        std::fs::write(mod_skse.join("Root/skse64_1_5_97.dll"), "skse dll").unwrap();
        // Also has a regular plugin
        std::fs::write(mod_skse.join("SKSE_Plugin.esp"), "skse plugin").unwrap();

        let mods = vec![("SKSE", mod_skse.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data", &mods, &overwrite).unwrap();

        // Root/ files should be at VFS root (alongside game exe)
        assert!(tree.root.resolve("skse64_loader.exe").is_some());
        assert!(tree.root.resolve("skse64_1_5_97.dll").is_some());
        // Regular mod files should be under Data/
        assert!(tree.root.resolve("Data/SKSE_Plugin.esp").is_some());
        // Game exe still accessible
        assert!(tree.root.resolve("SkyrimSE.exe").is_some());
    }

    #[test]
    fn test_full_game_vfs_mod_overrides_vanilla() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        // Create a mod that overrides a vanilla texture
        let mod_a = tmp.path().join("BetterTextures");
        std::fs::create_dir_all(mod_a.join("textures")).unwrap();
        std::fs::write(mod_a.join("textures/vanilla.dds"), "better version").unwrap();

        let mods = vec![("BetterTextures", mod_a.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data", &mods, &overwrite).unwrap();

        // The mod should win over vanilla
        let node = tree.root.resolve("Data/textures/vanilla.dds").unwrap();
        if let VfsNode::File { origin, .. } = node {
            assert_eq!(origin, "BetterTextures");
        } else {
            panic!("Expected file node");
        }
    }

    #[test]
    fn test_full_game_vfs_overwrite_wins() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(overwrite.join("Data")).unwrap();
        std::fs::write(overwrite.join("Data/generated.esp"), "overwrite plugin").unwrap();
        // Root-level overwrite file
        std::fs::write(overwrite.join("crash_log.txt"), "game crash").unwrap();

        let tree = build_full_game_vfs(&game_dir, "Data", &[], &overwrite).unwrap();

        // Overwrite Data/ content
        let node = tree.root.resolve("Data/generated.esp").unwrap();
        if let VfsNode::File { origin, .. } = node {
            assert_eq!(origin, "Overwrite");
        } else {
            panic!("Expected file node");
        }

        // Overwrite root-level content
        assert!(tree.root.resolve("crash_log.txt").is_some());
    }

    #[test]
    fn test_full_game_vfs_mod_overrides_overwrite() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(overwrite.join("Data/SKSE/Plugins")).unwrap();
        std::fs::write(
            overwrite.join("Data/SKSE/Plugins/SSEDisplayTweaks.ini"),
            "overwrite",
        )
        .unwrap();

        let mod_a = tmp.path().join("DisplayTweaksMod");
        std::fs::create_dir_all(mod_a.join("SKSE/Plugins")).unwrap();
        std::fs::write(mod_a.join("SKSE/Plugins/SSEDisplayTweaks.ini"), "mod").unwrap();

        let mods = vec![("DisplayTweaksMod", mod_a.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data", &mods, &overwrite).unwrap();

        let node = tree
            .root
            .resolve("Data/SKSE/Plugins/SSEDisplayTweaks.ini")
            .unwrap();
        if let VfsNode::File { origin, .. } = node {
            assert_eq!(origin, "DisplayTweaksMod");
        } else {
            panic!("Expected file node");
        }
    }

    #[test]
    fn test_full_game_vfs_data_files_morrowind() {
        let tmp = tempfile::tempdir().unwrap();
        // Morrowind uses "Data Files" instead of "Data"
        let game_dir = tmp.path().join("Morrowind");
        std::fs::create_dir_all(game_dir.join("Data Files")).unwrap();
        std::fs::write(game_dir.join("Morrowind.exe"), "game").unwrap();
        std::fs::write(game_dir.join("Data Files/Morrowind.esm"), "master").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        let mod_a = tmp.path().join("MyMod");
        std::fs::create_dir_all(&mod_a).unwrap();
        std::fs::write(mod_a.join("mod.esp"), "mod plugin").unwrap();

        let mods = vec![("MyMod", mod_a.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data Files", &mods, &overwrite).unwrap();

        // Game files
        assert!(tree.root.resolve("Morrowind.exe").is_some());
        assert!(tree.root.resolve("Data Files/Morrowind.esm").is_some());
        // Mod goes under "Data Files/"
        assert!(tree.root.resolve("Data Files/mod.esp").is_some());
    }

    #[test]
    fn test_full_game_vfs_priority_with_root() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        // Two mods both providing same Root/ file — higher priority wins
        let mod_a = tmp.path().join("ENB_A");
        std::fs::create_dir_all(mod_a.join("Root")).unwrap();
        std::fs::write(mod_a.join("Root/d3d11.dll"), "enb a").unwrap();

        let mod_b = tmp.path().join("ENB_B");
        std::fs::create_dir_all(mod_b.join("Root")).unwrap();
        std::fs::write(mod_b.join("Root/d3d11.dll"), "enb b").unwrap();

        // ENB_B is higher priority (last in list)
        let mods = vec![("ENB_A", mod_a.as_path()), ("ENB_B", mod_b.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data", &mods, &overwrite).unwrap();

        let node = tree.root.resolve("d3d11.dll").unwrap();
        if let VfsNode::File { origin, .. } = node {
            assert_eq!(origin, "ENB_B");
        } else {
            panic!("Expected file node");
        }
    }

    #[test]
    fn test_full_game_vfs_root_with_data_subfolder() {
        let tmp = tempfile::tempdir().unwrap();
        let game_dir = make_game_dir(tmp.path());
        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        // SKSE-style mod: Root/ + Data/ subfolders (mirrors game root structure)
        let mod_skse = tmp.path().join("SKSE");
        std::fs::create_dir_all(mod_skse.join("Root")).unwrap();
        std::fs::create_dir_all(mod_skse.join("Data/Scripts/Source")).unwrap();
        std::fs::write(mod_skse.join("Root/skse64_loader.exe"), "loader").unwrap();
        std::fs::write(mod_skse.join("Root/skse64_1_5_97.dll"), "dll").unwrap();
        std::fs::write(mod_skse.join("Data/Scripts/Source/Actor.psc"), "script").unwrap();

        let mods = vec![("SKSE", mod_skse.as_path())];
        let tree = build_full_game_vfs(&game_dir, "Data", &mods, &overwrite).unwrap();

        // Root/ files at VFS root
        assert!(tree.root.resolve("skse64_loader.exe").is_some());
        assert!(tree.root.resolve("skse64_1_5_97.dll").is_some());
        // Data/ files should be at Data/ (NOT Data/Data/)
        assert!(tree.root.resolve("Data/Scripts/Source/Actor.psc").is_some());
        // Must NOT be double-prefixed
        assert!(tree
            .root
            .resolve("Data/Data/Scripts/Source/Actor.psc")
            .is_none());
        // Game files still accessible
        assert!(tree.root.resolve("SkyrimSE.exe").is_some());
        assert!(tree.root.resolve("Data/Skyrim.esm").is_some());
    }

    #[test]
    fn test_case_insensitive_lookup() {
        let tmp = tempfile::tempdir().unwrap();

        let mod_a = tmp.path().join("ModA");
        std::fs::create_dir_all(mod_a.join("Textures")).unwrap();
        std::fs::write(mod_a.join("Textures/Armor.dds"), "data").unwrap();

        let overwrite = tmp.path().join("overwrite");
        std::fs::create_dir_all(&overwrite).unwrap();

        let mods = vec![("ModA", mod_a.as_path())];
        let tree = build_vfs_tree(&mods, &overwrite).unwrap();

        // Should find with different case
        assert!(tree.root.resolve("textures/armor.dds").is_some());
        assert!(tree.root.resolve("TEXTURES/ARMOR.DDS").is_some());
    }

    #[test]
    fn test_case_variants_do_not_duplicate_listing_entries() {
        let mut root = VfsNode::new_directory();
        let now = SystemTime::now();

        root.insert_file(
            &["Data", "CalienteTools", "foo.txt"],
            PathBuf::from("/tmp/a"),
            1,
            now,
            "ModA",
        );
        root.insert_file(
            &["data", "calientetools", "foo.txt"],
            PathBuf::from("/tmp/b"),
            1,
            now,
            "ModB",
        );

        let Some(data_node) = root.resolve("Data") else {
            panic!("missing Data node");
        };
        let children = data_node.list_children();
        let matches = children
            .iter()
            .filter(|(name, _)| name.eq_ignore_ascii_case("calientetools"))
            .count();

        assert_eq!(matches, 1, "expected one directory entry for case variants");
    }
}

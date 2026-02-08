//! File conflict detection between mods.
//!
//! When multiple mods provide the same file, the one with higher priority wins.
//! This module detects these conflicts and reports winning/losing status.

use std::collections::HashMap;
use std::path::PathBuf;

use crate::paths::normalize_for_lookup;

/// A file conflict between two mods.
#[derive(Debug, Clone)]
pub struct FileConflict {
    /// The relative path of the conflicting file (normalized)
    pub relative_path: String,
    /// The mod that wins (has the file in the VFS)
    pub winner: String,
    /// The mod that loses (file is hidden)
    pub loser: String,
}

/// Summary of conflicts for a single mod.
#[derive(Debug, Clone, Default)]
pub struct ModConflicts {
    /// Files this mod wins (overrides lower-priority mods)
    pub winning: Vec<FileConflict>,
    /// Files this mod loses (hidden by higher-priority mods)
    pub losing: Vec<FileConflict>,
}

impl ModConflicts {
    pub fn is_empty(&self) -> bool {
        self.winning.is_empty() && self.losing.is_empty()
    }

    pub fn has_winning(&self) -> bool {
        !self.winning.is_empty()
    }

    pub fn has_losing(&self) -> bool {
        !self.losing.is_empty()
    }
}

/// Detect all file conflicts between active mods.
///
/// `mods` is a list of (mod_name, mod_path) sorted by ascending priority
/// (index 0 = lowest priority, last = highest priority).
pub fn detect_conflicts(mods: &[(String, PathBuf)]) -> HashMap<String, ModConflicts> {
    let mut result: HashMap<String, ModConflicts> = HashMap::new();

    // Map: normalized_relative_path -> Vec<(mod_name, priority)>
    let mut file_owners: HashMap<String, Vec<(String, usize)>> = HashMap::new();

    for (priority, (mod_name, mod_path)) in mods.iter().enumerate() {
        if !mod_path.exists() {
            continue;
        }

        for entry in walkdir::WalkDir::new(mod_path)
            .into_iter()
            .filter_map(|e| e.ok())
            .filter(|e| e.file_type().is_file())
        {
            let relative = entry.path().strip_prefix(mod_path).unwrap_or(entry.path());

            let relative_str = relative.to_string_lossy();

            // Skip meta.ini - not a game file
            if relative_str == "meta.ini" {
                continue;
            }

            let normalized = normalize_for_lookup(&relative_str);
            file_owners
                .entry(normalized)
                .or_default()
                .push((mod_name.clone(), priority));
        }
    }

    // Find conflicts (files provided by more than one mod)
    for (path, owners) in &file_owners {
        if owners.len() < 2 {
            continue;
        }

        // The owner with highest priority wins
        let winner = owners.iter().max_by_key(|(_, p)| p).unwrap();

        for (mod_name, _priority) in owners {
            if mod_name == &winner.0 {
                // This mod wins this file
                for (loser_name, _) in owners {
                    if loser_name != mod_name {
                        result
                            .entry(mod_name.clone())
                            .or_default()
                            .winning
                            .push(FileConflict {
                                relative_path: path.clone(),
                                winner: mod_name.clone(),
                                loser: loser_name.clone(),
                            });
                    }
                }
            } else {
                // This mod loses this file
                result
                    .entry(mod_name.clone())
                    .or_default()
                    .losing
                    .push(FileConflict {
                        relative_path: path.clone(),
                        winner: winner.0.clone(),
                        loser: mod_name.clone(),
                    });
            }
        }
    }

    result
}

/// Quick check: does this mod have any conflicts?
pub fn has_conflicts(mod_name: &str, conflicts: &HashMap<String, ModConflicts>) -> bool {
    conflicts
        .get(mod_name)
        .map(|c| !c.is_empty())
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_no_conflicts() {
        let tmp = tempfile::tempdir().unwrap();

        let mod_a = tmp.path().join("ModA");
        let mod_b = tmp.path().join("ModB");
        std::fs::create_dir_all(mod_a.join("textures")).unwrap();
        std::fs::create_dir_all(mod_b.join("meshes")).unwrap();
        std::fs::write(mod_a.join("textures/a.dds"), "a").unwrap();
        std::fs::write(mod_b.join("meshes/b.nif"), "b").unwrap();

        let mods = vec![("ModA".to_string(), mod_a), ("ModB".to_string(), mod_b)];

        let conflicts = detect_conflicts(&mods);
        assert!(!has_conflicts("ModA", &conflicts));
        assert!(!has_conflicts("ModB", &conflicts));
    }

    #[test]
    fn test_simple_conflict() {
        let tmp = tempfile::tempdir().unwrap();

        let mod_a = tmp.path().join("ModA");
        let mod_b = tmp.path().join("ModB");
        std::fs::create_dir_all(mod_a.join("textures")).unwrap();
        std::fs::create_dir_all(mod_b.join("textures")).unwrap();
        std::fs::write(mod_a.join("textures/shared.dds"), "a version").unwrap();
        std::fs::write(mod_b.join("textures/shared.dds"), "b version").unwrap();

        // ModB has higher priority (index 1)
        let mods = vec![("ModA".to_string(), mod_a), ("ModB".to_string(), mod_b)];

        let conflicts = detect_conflicts(&mods);

        // ModB wins, ModA loses
        let mod_b_conflicts = conflicts.get("ModB").unwrap();
        assert!(mod_b_conflicts.has_winning());
        assert!(!mod_b_conflicts.has_losing());

        let mod_a_conflicts = conflicts.get("ModA").unwrap();
        assert!(!mod_a_conflicts.has_winning());
        assert!(mod_a_conflicts.has_losing());
    }
}

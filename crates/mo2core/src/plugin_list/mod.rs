//! ESP/ESM/ESL plugin load order management.
//!
//! Manages the load order of game plugins (.esp, .esm, .esl files).
//! Handles:
//! - Reading plugins from mod directories
//! - Maintaining load order with locked positions
//! - Force-loaded plugins (game masters, CC content)
//! - Light plugin (ESL) detection

use std::collections::{HashMap, HashSet};
use std::path::PathBuf;

use crate::config::locked::LockedOrder;
use crate::config::plugins::{LoadOrder, PluginEntry, PluginsTxt};

/// Information about a single plugin.
#[derive(Debug, Clone)]
pub struct PluginInfo {
    /// Plugin filename (e.g., `Skyrim.esm`)
    pub filename: String,
    /// Whether the plugin is enabled
    pub enabled: bool,
    /// Load order priority (0 = loaded first)
    pub priority: i32,
    /// Whether this plugin is force-loaded (game master, etc.)
    pub force_loaded: bool,
    /// Whether this plugin is force-enabled (can't be disabled)
    pub force_enabled: bool,
    /// Whether this plugin is force-disabled
    pub force_disabled: bool,
    /// Whether the plugin has the master flag set (.esm or flagged .esp)
    pub is_master: bool,
    /// Whether the plugin is a light plugin (.esl or flagged)
    pub is_light: bool,
    /// Which mod provides this plugin
    pub origin_mod: Option<String>,
    /// Full path to the plugin file
    pub file_path: Option<PathBuf>,
    /// Master files this plugin depends on
    pub masters: Vec<String>,
    /// Whether the plugin's load order is locked
    pub is_locked: bool,
    /// The locked priority (if locked)
    pub locked_priority: Option<i32>,
}

/// Manages the complete plugin list.
#[derive(Debug, Clone)]
pub struct PluginList {
    /// All known plugins
    pub plugins: Vec<PluginInfo>,
    /// Quick lookup by filename (case-insensitive)
    index: HashMap<String, usize>,
}

impl PluginList {
    /// Create a new empty plugin list.
    pub fn new() -> Self {
        PluginList {
            plugins: Vec::new(),
            index: HashMap::new(),
        }
    }

    /// Build the plugin list from profile data and mod directories.
    ///
    /// The `mod_dirs` list should include the game's Data directory as the first
    /// entry with name `"<Game>"` for base game plugin detection.
    pub fn build(
        plugins_txt: &PluginsTxt,
        load_order: &LoadOrder,
        locked_order: &LockedOrder,
        primary_plugins: &[String],
        mod_dirs: &[(String, PathBuf, bool)], // (mod_name, mod_path, enabled)
    ) -> Self {
        let mut list = PluginList::new();
        let locked_map = locked_order.to_map();
        let primary_lookup: HashSet<String> =
            primary_plugins.iter().map(|p| p.to_lowercase()).collect();

        // Scan all directories for plugins, separating game vs mod plugins
        let mut game_plugins: Vec<(String, String, PathBuf)> = Vec::new(); // (lower, filename, path)
        let mut mod_plugins: Vec<(String, String, PathBuf, String, usize)> = Vec::new(); // (lower, filename, path, mod_name, mod_order)

        for (mod_order, (mod_name, mod_path, enabled)) in mod_dirs.iter().enumerate() {
            if !enabled {
                continue;
            }
            let is_game = mod_name == "<Game>";
            if let Ok(entries) = std::fs::read_dir(mod_path) {
                for entry in entries.filter_map(|e| e.ok()) {
                    let filename = entry.file_name().to_string_lossy().to_string();
                    if is_plugin_file(&filename) {
                        let lower = filename.to_lowercase();
                        if is_game {
                            game_plugins.push((lower, filename, entry.path()));
                        } else {
                            mod_plugins.push((
                                lower,
                                filename,
                                entry.path(),
                                mod_name.clone(),
                                mod_order,
                            ));
                        }
                    }
                }
            }
        }

        // Sort game plugins: .esm first, then .esl, then .esp (alphabetical within each)
        game_plugins.sort_by(|a, b| {
            let ext_a = plugin_sort_key(&a.0);
            let ext_b = plugin_sort_key(&b.0);
            ext_a.cmp(&ext_b).then(a.0.cmp(&b.0))
        });

        // MO2-style effective order:
        // primary plugins first, then loadorder.txt entries not already present.
        let effective_order = merge_primary_and_load_order(primary_plugins, &load_order.plugins);

        if !effective_order.is_empty() {
            for (priority, plugin_name) in effective_order.iter().enumerate() {
                let lower = plugin_name.to_lowercase();
                let is_primary = primary_lookup.contains(&lower);

                // Check if it's a game plugin or mod plugin and prefer discovered filename case.
                let (resolved_name, origin, file_path) =
                    if let Some(mp) = mod_plugins.iter().find(|(l, _, _, _, _)| *l == lower) {
                        (mp.1.clone(), Some(mp.3.clone()), Some(mp.2.clone()))
                    } else if let Some(gp) = game_plugins.iter().find(|(l, _, _)| *l == lower) {
                        (gp.1.clone(), Some("<Game>".to_string()), Some(gp.2.clone()))
                    } else {
                        (plugin_name.clone(), None, None)
                    };

                let is_locked = locked_map.contains_key(&lower);
                let locked_priority = locked_map.get(&lower).copied();
                let ext = extension_lower(&resolved_name);
                // plugins.txt can omit entries; if loadorder/primary includes a plugin,
                // default it to enabled unless explicitly disabled in plugins.txt.
                let enabled = if is_primary || ext == "esm" {
                    true
                } else {
                    plugins_txt
                        .find(&resolved_name)
                        .map(|e| e.enabled)
                        .unwrap_or(true)
                };

                let info = PluginInfo {
                    filename: resolved_name,
                    enabled,
                    priority: priority as i32,
                    force_loaded: is_primary || ext == "esm",
                    force_enabled: is_primary || ext == "esm",
                    force_disabled: false,
                    is_master: ext == "esm",
                    is_light: ext == "esl",
                    origin_mod: origin,
                    file_path,
                    masters: Vec::new(),
                    is_locked,
                    locked_priority,
                };

                let idx = list.plugins.len();
                list.index.insert(lower, idx);
                list.plugins.push(info);
            }
        }

        // Add game plugins not yet in the list (base game .esm/.esl/.esp)
        let mut next_priority = list.plugins.len() as i32;
        for (lower, filename, path) in &game_plugins {
            if list.index.contains_key(lower.as_str()) {
                continue;
            }
            let is_primary = primary_lookup.contains(lower);
            let ext = extension_lower(filename);
            let enabled = if is_primary || ext == "esm" {
                true
            } else {
                plugins_txt
                    .find(filename)
                    .map(|e| e.enabled)
                    .unwrap_or(true)
            }; // Base game plugins default enabled

            let info = PluginInfo {
                filename: filename.clone(),
                enabled,
                priority: next_priority,
                force_loaded: is_primary || ext == "esm",
                force_enabled: is_primary || ext == "esm",
                force_disabled: false,
                is_master: ext == "esm",
                is_light: ext == "esl",
                origin_mod: Some("<Game>".to_string()),
                file_path: Some(path.clone()),
                masters: Vec::new(),
                is_locked: locked_map.contains_key(lower),
                locked_priority: locked_map.get(lower).copied(),
            };

            let idx = list.plugins.len();
            list.index.insert(lower.clone(), idx);
            list.plugins.push(info);
            next_priority += 1;
        }

        // Add mod plugins not yet in the list
        // Build a deduped map (later mods = higher priority override earlier)
        let mut mod_plugin_map: HashMap<String, (String, PathBuf, String, usize)> = HashMap::new();
        for (lower, filename, path, mod_name, mod_order) in &mod_plugins {
            mod_plugin_map.insert(
                lower.clone(),
                (filename.clone(), path.clone(), mod_name.clone(), *mod_order),
            );
        }
        let mut mod_discovered: Vec<(String, String, PathBuf, String, usize)> = mod_plugin_map
            .into_iter()
            .filter(|(lower, _)| !list.index.contains_key(lower.as_str()))
            .map(|(lower, (filename, path, mod_name, mod_order))| {
                (lower, filename, path, mod_name, mod_order)
            })
            .collect();
        // MO2 uses game/plugin metadata in fallback ordering. We don't have full parity yet,
        // but mod-priority-based ordering is a better approximation than alphabetical.
        mod_discovered.sort_by(|a, b| a.4.cmp(&b.4).then(a.0.cmp(&b.0)));

        for (lower, filename, path, mod_name, _) in mod_discovered {
            let is_primary = primary_lookup.contains(&lower);
            let ext = extension_lower(&filename);
            let enabled = if is_primary || ext == "esm" {
                true
            } else {
                plugins_txt
                    .find(&filename)
                    .map(|e| e.enabled)
                    .unwrap_or(true)
            }; // Default new mod plugins to enabled

            let info = PluginInfo {
                filename,
                enabled,
                priority: next_priority,
                force_loaded: is_primary,
                force_enabled: is_primary,
                force_disabled: false,
                is_master: ext == "esm",
                is_light: ext == "esl",
                origin_mod: Some(mod_name),
                file_path: Some(path),
                masters: Vec::new(),
                is_locked: locked_map.contains_key(&lower),
                locked_priority: locked_map.get(&lower).copied(),
            };

            let idx = list.plugins.len();
            list.index.insert(lower, idx);
            list.plugins.push(info);
            next_priority += 1;
        }

        list
    }

    /// Get a plugin by filename (case-insensitive).
    pub fn find(&self, filename: &str) -> Option<&PluginInfo> {
        self.index
            .get(&filename.to_lowercase())
            .map(|&idx| &self.plugins[idx])
    }

    /// Get a mutable plugin by filename (case-insensitive).
    pub fn find_mut(&mut self, filename: &str) -> Option<&mut PluginInfo> {
        self.index
            .get(&filename.to_lowercase())
            .copied()
            .map(|idx| &mut self.plugins[idx])
    }

    /// Set the enabled state of a plugin.
    pub fn set_enabled(&mut self, filename: &str, enabled: bool) -> bool {
        if let Some(plugin) = self.find_mut(filename) {
            if plugin.force_enabled || plugin.force_disabled {
                return false; // Can't change forced plugins
            }
            plugin.enabled = enabled;
            true
        } else {
            false
        }
    }

    /// Get enabled plugins in load order.
    pub fn enabled_in_order(&self) -> Vec<&PluginInfo> {
        let mut enabled: Vec<&PluginInfo> = self.plugins.iter().filter(|p| p.enabled).collect();
        enabled.sort_by_key(|p| p.priority);
        enabled
    }

    /// Get total plugin count.
    pub fn count(&self) -> usize {
        self.plugins.len()
    }

    /// Get enabled plugin count.
    pub fn enabled_count(&self) -> usize {
        self.plugins.iter().filter(|p| p.enabled).count()
    }

    /// Export to PluginsTxt format.
    pub fn to_plugins_txt(&self) -> PluginsTxt {
        let entries = self
            .plugins
            .iter()
            .map(|p| PluginEntry {
                filename: p.filename.clone(),
                enabled: p.enabled,
            })
            .collect();
        PluginsTxt { entries }
    }

    /// Export to LoadOrder format.
    pub fn to_load_order(&self) -> LoadOrder {
        let mut sorted = self.plugins.clone();
        sorted.sort_by_key(|p| p.priority);
        LoadOrder {
            plugins: sorted.into_iter().map(|p| p.filename).collect(),
        }
    }
}

impl Default for PluginList {
    fn default() -> Self {
        Self::new()
    }
}

/// Check if a filename is a plugin file.
pub fn is_plugin_file(filename: &str) -> bool {
    let lower = filename.to_lowercase();
    lower.ends_with(".esp") || lower.ends_with(".esm") || lower.ends_with(".esl")
}

/// Get the lowercase extension of a filename.
fn extension_lower(filename: &str) -> String {
    filename.rsplit('.').next().unwrap_or("").to_lowercase()
}

/// Sort key for plugin types: .esm=0, .esl=1, .esp=2
fn plugin_sort_key(lower_filename: &str) -> u8 {
    if lower_filename.ends_with(".esm") {
        0
    } else if lower_filename.ends_with(".esl") {
        1
    } else {
        2
    }
}

fn merge_primary_and_load_order(
    primary_plugins: &[String],
    load_order_plugins: &[String],
) -> Vec<String> {
    let mut merged = Vec::new();
    let mut seen = HashSet::new();

    for plugin in primary_plugins {
        let lower = plugin.to_lowercase();
        if seen.insert(lower) {
            merged.push(plugin.clone());
        }
    }

    for plugin in load_order_plugins {
        let lower = plugin.to_lowercase();
        if seen.insert(lower) {
            merged.push(plugin.clone());
        }
    }

    merged
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_plugin_file() {
        assert!(is_plugin_file("Skyrim.esm"));
        assert!(is_plugin_file("MyMod.esp"));
        assert!(is_plugin_file("Light.esl"));
        assert!(is_plugin_file("UPPER.ESP"));
        assert!(!is_plugin_file("texture.dds"));
        assert!(!is_plugin_file("readme.txt"));
    }

    #[test]
    fn test_empty_list() {
        let list = PluginList::new();
        assert_eq!(list.count(), 0);
        assert_eq!(list.enabled_count(), 0);
        assert!(list.find("test.esp").is_none());
    }

    #[test]
    fn test_build_basic() {
        let mut plugins_txt = PluginsTxt::default();
        plugins_txt.entries.push(PluginEntry {
            filename: "Skyrim.esm".to_string(),
            enabled: true,
        });
        plugins_txt.entries.push(PluginEntry {
            filename: "MyMod.esp".to_string(),
            enabled: true,
        });
        plugins_txt.entries.push(PluginEntry {
            filename: "Disabled.esp".to_string(),
            enabled: false,
        });

        let load_order = LoadOrder {
            plugins: vec![
                "Skyrim.esm".to_string(),
                "MyMod.esp".to_string(),
                "Disabled.esp".to_string(),
            ],
        };

        let list = PluginList::build(&plugins_txt, &load_order, &LockedOrder::default(), &[], &[]);

        assert_eq!(list.count(), 3);
        assert_eq!(list.enabled_count(), 2);

        let skyrim = list.find("Skyrim.esm").unwrap();
        assert!(skyrim.enabled);
        assert!(skyrim.is_master);
        assert_eq!(skyrim.priority, 0);

        let disabled = list.find("disabled.esp").unwrap();
        assert!(!disabled.enabled);
    }

    #[test]
    fn test_set_enabled() {
        let plugins_txt = PluginsTxt {
            entries: vec![PluginEntry {
                filename: "Test.esp".to_string(),
                enabled: false,
            }],
        };
        let load_order = LoadOrder {
            plugins: vec!["Test.esp".to_string()],
        };

        let mut list =
            PluginList::build(&plugins_txt, &load_order, &LockedOrder::default(), &[], &[]);

        assert!(!list.find("Test.esp").unwrap().enabled);
        assert!(list.set_enabled("Test.esp", true));
        assert!(list.find("Test.esp").unwrap().enabled);
    }

    #[test]
    fn test_primary_plugins_are_prepended_and_forced_enabled() {
        let plugins_txt = PluginsTxt {
            entries: vec![
                PluginEntry {
                    filename: "Skyrim.esm".to_string(),
                    enabled: false,
                },
                PluginEntry {
                    filename: "MyMod.esp".to_string(),
                    enabled: true,
                },
            ],
        };
        let load_order = LoadOrder {
            plugins: vec!["MyMod.esp".to_string(), "Skyrim.esm".to_string()],
        };
        let primary = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];

        let list = PluginList::build(
            &plugins_txt,
            &load_order,
            &LockedOrder::default(),
            &primary,
            &[],
        );
        let skyrim = list.find("Skyrim.esm").unwrap();
        let update = list.find("Update.esm").unwrap();
        let my_mod = list.find("MyMod.esp").unwrap();

        assert_eq!(skyrim.priority, 0);
        assert_eq!(update.priority, 1);
        assert_eq!(my_mod.priority, 2);
        assert!(skyrim.enabled);
        assert!(skyrim.force_enabled);
        assert!(update.enabled);
        assert!(update.force_enabled);
    }
}

use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::rc::Rc;

use mo2core::config::executables::{Executable, ExecutablesList};
use mo2core::config::modlist::ModList;
use mo2core::conflict::{self, ModConflicts};
use mo2core::download;
use mo2core::gamedef::GameDef;
use mo2core::gamedef::GameId;
use mo2core::global_settings::GlobalSettings;
use mo2core::instance::{
    create_global_instance, create_portable_instance, list_global_instances,
    portable_instance_info, Instance,
};
use mo2core::launcher::process::LaunchConfig;
use mo2core::launcher::vfs as vfs_launch;
use mo2core::plugin_list::PluginList;
use mo2core::saves;
use mo2fuse::mount_manager::MountManager;
use mo2fuse::overlay::{build_full_game_vfs, VfsTree};
use mo2fuse::FuseController;
use slint::winit_030::{winit, EventResult as WinitEventResult, WinitWindowAccessor};
use slint::{LogicalSize, Model, ModelRc, SharedString, Timer, TimerMode, VecModel};

slint::include_modules!();

struct AppState {
    instance: Option<Instance>,
    /// Full-game VFS mount manager (mounts at <instance>/VFS_FUSE/)
    mount_manager: Option<MountManager>,
    global_settings: GlobalSettings,
    executables: ExecutablesList,
    /// Cached conflict data (expensive to compute)
    conflict_cache: HashMap<String, ModConflicts>,
    /// Cached VFS tree for data tab
    vfs_tree: Option<VfsTree>,
    /// Expanded paths in the data tab tree view
    data_expanded: HashSet<String>,
    /// Collapsed separator names (session-only, not persisted)
    collapsed_separators: HashSet<String>,
    /// PID of currently running launched process (for lock state)
    locked_pid: Option<u32>,
}

fn prefer_prefix_separator_style(entries: &[mo2core::config::modlist::ModListEntry]) -> bool {
    let mut prefix_count = 0usize;
    let mut suffix_count = 0usize;
    for e in entries {
        let lower = e.name.to_lowercase();
        if lower.starts_with("_separator_") || lower == "_separator" {
            prefix_count += 1;
        } else if lower.ends_with("_separator") {
            suffix_count += 1;
        }
    }
    prefix_count > suffix_count
}

fn build_separator_name_for_profile(
    display_name: &str,
    entries: &[mo2core::config::modlist::ModListEntry],
) -> String {
    let trimmed = display_name.trim();
    if prefer_prefix_separator_style(entries) {
        if trimmed.is_empty() {
            "_separator_Separator".to_string()
        } else {
            format!("_separator_{trimmed}")
        }
    } else {
        ModList::separator_entry_name(trimmed)
    }
}

/// Build known game names from NaK's game database.
fn known_game_names() -> Vec<&'static str> {
    nak_rust::game_finder::KNOWN_GAMES
        .iter()
        .map(|g| g.name)
        .collect()
}

fn main() {
    tracing_subscriber::fmt::init();

    if let Err(e) = slint::BackendSelector::new()
        .backend_name("winit".to_string())
        .select()
    {
        panic!("Failed to select Slint winit backend (required for drag-and-drop): {e}");
    }

    let global_settings = match GlobalSettings::load() {
        Ok(gs) => gs,
        Err(e) => {
            tracing::error!("Failed to load global settings: {e}");
            GlobalSettings::load().unwrap_or_else(|_| {
                // Fallback: create default in-memory settings
                panic!("Cannot initialize global settings: {e}");
            })
        }
    };

    let ui = MainWindow::new().unwrap();
    // Use a consistent baseline size and let the window manager place the window.
    ui.window().set_size(LogicalSize::new(1200.0, 700.0));

    let state = Rc::new(RefCell::new(AppState {
        instance: None,
        mount_manager: None,
        global_settings,
        executables: ExecutablesList::default(),
        conflict_cache: HashMap::new(),
        vfs_tree: None,
        data_expanded: HashSet::new(),
        collapsed_separators: HashSet::new(),
        locked_pid: None,
    }));

    // Set known games for wizard (from NaK's game database)
    let game_names: Vec<SharedString> = known_game_names()
        .iter()
        .map(|g| SharedString::from(*g))
        .collect();
    ui.set_known_games(ModelRc::new(VecModel::from(game_names)));

    // External file drag-and-drop (winit backend): dropped archives open Install Mod dialog.
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.window().on_winit_window_event(move |_window, event| {
            if let winit::event::WindowEvent::DroppedFile(path) = event {
                tracing::info!("Window dropped file: {}", path.display());
                if is_supported_mod_archive(path) {
                    if let Some(ui) = ui_handle.upgrade() {
                        open_install_dialog_for_archive(&ui, &state, path);
                    }
                } else {
                    tracing::info!(
                        "Dropped file is not a supported archive type: {}",
                        path.display()
                    );
                }
            }
            WinitEventResult::Propagate
        });
    }

    // Populate instance list
    refresh_instance_list(&ui, &state.borrow().global_settings);

    // Try auto-load last instance
    {
        let last = state
            .borrow()
            .global_settings
            .last_instance()
            .map(String::from);
        if let Some(ref path_str) = last {
            let path = PathBuf::from(path_str);
            if path.join("ModOrganizer.ini").exists() {
                let mut st = state.borrow_mut();
                if let Ok(()) = load_instance(&ui, &mut st, &path) {
                    ui.set_current_page(1);
                }
            }
        }
    }

    // =====================================================
    // Instance Manager callbacks
    // =====================================================

    // --- Open Selected Instance ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_open_selected_instance(move || {
            let ui = ui_handle.unwrap();
            let sel = ui.get_selected_instance();
            if sel < 0 {
                return;
            }
            let instance_list = ui.get_instance_list();
            let idx = sel as usize;
            if idx >= instance_list.row_count() {
                return;
            }
            let entry = instance_list.row_data(idx).unwrap();
            let path = PathBuf::from(entry.path.to_string());

            let mut st = state.borrow_mut();
            if let Ok(()) = load_instance(&ui, &mut st, &path) {
                ui.set_current_page(1);
            }
        });
    }

    // --- Browse Portable ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_browse_portable(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            if let Ok(()) = load_instance(&ui, &mut st, &folder) {
                ui.set_current_page(1);
            }
        });
    }

    // --- Remove Selected Instance ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_remove_selected_instance(move || {
            let ui = ui_handle.unwrap();
            let sel = ui.get_selected_instance();
            if sel < 0 {
                return;
            }
            let model = ui.get_instance_list();
            let idx = sel as usize;
            if idx >= model.row_count() {
                return;
            }
            let Some(entry) = model.row_data(idx) else {
                return;
            };
            let instance_path = entry.path.to_string();
            let instance_name = entry.name.to_string();

            let confirm = rfd::MessageDialog::new()
                .set_title("Remove Instance")
                .set_level(rfd::MessageLevel::Warning)
                .set_description(format!(
                    "Delete instance '{}'?\n\n{}\n\nThis permanently deletes the instance folder.",
                    instance_name, instance_path
                ))
                .set_buttons(rfd::MessageButtons::YesNo)
                .show();
            if !matches!(confirm, rfd::MessageDialogResult::Yes) {
                return;
            }

            let mut st = state.borrow_mut();
            let target = PathBuf::from(&instance_path);

            if st
                .instance
                .as_ref()
                .map(|i| i.root == target)
                .unwrap_or(false)
            {
                if let Some(mut mm) = st.mount_manager.take() {
                    if let Err(e) = mm.unmount() {
                        tracing::error!("Failed to unmount VFS before deleting instance: {e}");
                    }
                }
                st.instance = None;
                st.executables = ExecutablesList::default();
                st.conflict_cache.clear();
                ui.set_fuse_mounted(false);
            }

            match std::fs::remove_dir_all(&target) {
                Ok(()) => {
                    st.global_settings.remove_recent_instance(&instance_path);
                    st.global_settings.clear_last_instance_if(&instance_path);
                    if let Err(e) = st.global_settings.save() {
                        tracing::warn!("Failed to save global settings after deletion: {e}");
                    }
                    refresh_instance_list(&ui, &st.global_settings);
                }
                Err(e) => {
                    tracing::error!("Failed to delete instance '{}': {e}", instance_path);
                    let _ = rfd::MessageDialog::new()
                        .set_title("Delete Failed")
                        .set_level(rfd::MessageLevel::Error)
                        .set_description(format!(
                            "Failed to delete instance folder:\n{}\n\n{}",
                            instance_path, e
                        ))
                        .set_buttons(rfd::MessageButtons::Ok)
                        .show();
                }
            }
        });
    }

    // --- Create New Instance (go to wizard) ---
    {
        let ui_handle = ui.as_weak();
        ui.on_create_new_instance(move || {
            let ui = ui_handle.unwrap();
            ui.set_wizard_step(0);
            ui.set_wizard_is_portable(false);
            ui.set_wizard_game_name(SharedString::default());
            ui.set_wizard_game_path(SharedString::default());
            ui.set_wizard_instance_name(SharedString::default());
            ui.set_wizard_location(SharedString::default());
            ui.set_current_page(2);
        });
    }

    // =====================================================
    // Creation Wizard callbacks
    // =====================================================

    // --- Wizard Finish ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_wizard_finish(move |is_portable, game_name, name_or_path| {
            let ui = ui_handle.unwrap();
            let game = game_name.to_string();
            let target = name_or_path.to_string();

            if game.trim().is_empty() {
                let _ = rfd::MessageDialog::new()
                    .set_title("Create Instance Failed")
                    .set_level(rfd::MessageLevel::Error)
                    .set_description("Please select a game before creating an instance.")
                    .set_buttons(rfd::MessageButtons::Ok)
                    .show();
                return;
            }
            if target.trim().is_empty() {
                let _ = rfd::MessageDialog::new()
                    .set_title("Create Instance Failed")
                    .set_level(rfd::MessageLevel::Error)
                    .set_description(if is_portable {
                        "Please choose a portable instance location."
                    } else {
                        "Please provide an instance name."
                    })
                    .set_buttons(rfd::MessageButtons::Ok)
                    .show();
                return;
            }

            let result = if is_portable {
                let path = PathBuf::from(target);
                create_portable_instance(&path, &game)
            } else {
                let name = target;
                create_global_instance(&name, &game)
            };

            match result {
                Ok(mut instance) => {
                    let root = instance.root.clone();
                    // Save game path to INI if set
                    let game_path_str = ui.get_wizard_game_path().to_string();
                    if !game_path_str.is_empty() {
                        instance
                            .config
                            .set_game_directory(Path::new(&game_path_str));
                        let ini_path = root.join("ModOrganizer.ini");
                        if let Err(e) = instance.config.write(&ini_path) {
                            tracing::error!("Failed to save game path to INI: {e}");
                        }
                    }
                    let mut st = state.borrow_mut();
                    st.instance = Some(instance);
                    st.executables = ExecutablesList::default();
                    // Compute conflict cache
                    refresh_conflict_cache(&mut st);
                    // Auto-mount VFS
                    auto_mount_vfs(&mut st);
                    // Save as last instance
                    st.global_settings
                        .set_last_instance(&root.display().to_string());
                    let _ = st.global_settings.save();
                    refresh_ui(&ui, &st);
                    ui.set_instance_path(root.display().to_string().into());
                    ui.set_current_page(1);
                    // Refresh instance list for when they come back
                    refresh_instance_list(&ui, &st.global_settings);
                }
                Err(e) => {
                    tracing::error!("Failed to create instance: {e}");
                    let _ = rfd::MessageDialog::new()
                        .set_title("Create Instance Failed")
                        .set_level(rfd::MessageLevel::Error)
                        .set_description(format!("Could not create instance.\n\n{}", e))
                        .set_buttons(rfd::MessageButtons::Ok)
                        .show();
                }
            }
        });
    }

    // --- Wizard Cancel ---
    {
        let ui_handle = ui.as_weak();
        ui.on_wizard_cancel(move || {
            let ui = ui_handle.unwrap();
            ui.set_current_page(0);
        });
    }

    // --- Wizard Browse Location ---
    {
        let ui_handle = ui.as_weak();
        ui.on_wizard_browse_location(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_wizard_location(folder.display().to_string().into());
        });
    }

    // --- Wizard Browse Game Path ---
    {
        let ui_handle = ui.as_weak();
        ui.on_wizard_browse_game_path(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_wizard_game_path(folder.display().to_string().into());
        });
    }

    // =====================================================
    // Main App callbacks
    // =====================================================

    // --- Set Profile ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_set_profile(move |name| {
            let mut st = state.borrow_mut();
            let mut switched = false;
            if let Some(ref mut instance) = st.instance {
                if let Err(e) = instance.set_active_profile(&name) {
                    tracing::error!("Failed to set profile: {e}");
                    return;
                }
                switched = true;
                // Refresh conflict cache for new profile
                refresh_conflict_cache(&mut st);
                let ui = ui_handle.unwrap();
                refresh_ui(&ui, &st);
                // Rebuild VFS if mounted
                rebuild_vfs_if_mounted(&mut st);
            }

            if switched {
                // Immediately retarget profile-local INIs/saves in the active prefix.
                deploy_plugins_to_prefix(&st);
            }
        });
    }

    // --- Toggle Mod ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_toggle_mod(move |mod_name_str| {
            let mut st = state.borrow_mut();
            if let Some(ref mut instance) = st.instance {
                let mod_name = mod_name_str.to_string();
                let Some(idx) = instance.mods.iter().position(|m| m.name == mod_name) else {
                    tracing::warn!("toggle_mod: mod '{}' not found", mod_name);
                    return;
                };
                let new_enabled = !instance.mods[idx].enabled;

                if let Some(ref mut profile) = instance.active_profile {
                    profile.set_mod_enabled(&mod_name, new_enabled);
                    // Sync priority back from modlist (handles newly added mods)
                    if let Some(p) = profile.mod_priority(&mod_name) {
                        instance.mods[idx].priority = p;
                    }
                }
                instance.mods[idx].enabled = new_enabled;

                // Persist modlist.txt
                save_active_modlist(instance);

                // Refresh conflict cache (mod enable/disable changes conflicts)
                refresh_conflict_cache(&mut st);

                let ui = ui_handle.unwrap();
                refresh_ui(&ui, &st);
                // Auto-rebuild VFS
                rebuild_vfs_if_mounted(&mut st);
            }
        });
    }

    // --- Select Mod ---
    {
        let ui_handle = ui.as_weak();
        ui.on_select_mod(move |mod_name| {
            let ui = ui_handle.unwrap();
            ui.set_selected_mod_name(mod_name.clone());

            let model = ui.get_mod_list();
            let selected = mod_name.to_string();
            let mut win_keys = String::new();
            let mut lose_keys = String::new();
            let mut win_text = SharedString::default();
            let mut lose_text = SharedString::default();

            for i in 0..model.row_count() {
                if let Some(row) = model.row_data(i) {
                    if row.name.as_str() == selected.as_str() {
                        win_keys = row.winning_opponent_keys.to_string();
                        lose_keys = row.losing_opponent_keys.to_string();
                        win_text = row.winning_opponents;
                        lose_text = row.losing_opponents;
                        break;
                    }
                }
            }

            if win_keys.is_empty() && lose_keys.is_empty() {
                ui.set_selected_mod_winning(SharedString::default());
                ui.set_selected_mod_losing(SharedString::default());
            } else {
                ui.set_selected_mod_winning(win_text);
                ui.set_selected_mod_losing(lose_text);
            }

            for i in 0..model.row_count() {
                let Some(mut row) = model.row_data(i) else {
                    continue;
                };
                if selected.is_empty()
                    || row.name.as_str() == selected.as_str()
                    || row.is_separator
                    || row.is_overwrite
                {
                    row.highlight_winning = false;
                    row.highlight_losing = false;
                } else {
                    let token = format!("|{}|", row.name);
                    let is_losing_peer = lose_keys.contains(&token);
                    let is_winning_peer = win_keys.contains(&token);
                    row.highlight_losing = is_losing_peer;
                    row.highlight_winning = !is_losing_peer && is_winning_peer;
                }
                model.set_row_data(i, row);
            }
        });
    }

    // --- Toggle Separator Collapse ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_toggle_separator_collapse(move |sep_name| {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            let name = sep_name.to_string();
            let column = ui.get_sort_column();
            let ascending = ui.get_sort_ascending();

            // Ignore toggles for empty separators (no mods until next separator).
            let can_collapse = {
                let Some(instance) = st.instance.as_ref() else {
                    return;
                };
                let mut entries = build_mod_list(instance, &st.conflict_cache);
                entries.retain(|e| !e.is_overwrite);
                sort_mod_entries(&mut entries, column, ascending);
                apply_collapse_state(&mut entries, &st.collapsed_separators);
                let can = entries
                    .iter()
                    .find(|e| e.name.as_str() == name.as_str() && e.is_separator)
                    .map(|e| e.separator_has_children)
                    .unwrap_or(false);
                if !can {
                    apply_selection_state_to_mod_entries(&ui, &mut entries);
                    ui.set_mod_list(ModelRc::new(VecModel::from(entries)));
                }
                can
            };
            if !can_collapse {
                return;
            }

            if st.collapsed_separators.contains(&name) {
                st.collapsed_separators.remove(&name);
            } else {
                st.collapsed_separators.insert(name);
            }
            // Rebuild mod list with updated collapse state
            let Some(instance) = st.instance.as_ref() else {
                return;
            };
            let mut entries = build_mod_list(instance, &st.conflict_cache);
            entries.retain(|e| !e.is_overwrite);
            sort_mod_entries(&mut entries, column, ascending);
            apply_collapse_state(&mut entries, &st.collapsed_separators);
            apply_selection_state_to_mod_entries(&ui, &mut entries);
            ui.set_mod_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- FUSE Mount ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_fuse_mount(move || {
            let mut st = state.borrow_mut();
            auto_mount_vfs(&mut st);
            let ui = ui_handle.unwrap();
            ui.set_fuse_mounted(is_vfs_mounted(&st));
        });
    }

    // --- FUSE Unmount ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_fuse_unmount(move || {
            let mut st = state.borrow_mut();
            if let Some(mut mm) = st.mount_manager.take() {
                if let Err(e) = mm.unmount() {
                    tracing::error!("Failed to unmount VFS: {e}");
                }
            }
            // Refresh conflict cache & VFS tree since overwrite may have new files
            refresh_conflict_cache(&mut st);
            let ui = ui_handle.unwrap();
            ui.set_fuse_mounted(false);
            refresh_ui(&ui, &st);
        });
    }

    // --- Switch Instance ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_switch_instance(move || {
            let mut st = state.borrow_mut();
            // Unmount VFS
            if let Some(mut mm) = st.mount_manager.take() {
                if let Err(e) = mm.unmount() {
                    tracing::error!("Failed to unmount VFS on instance switch: {e}");
                }
            }
            st.instance = None;
            st.executables = ExecutablesList::default();
            st.conflict_cache.clear();
            let ui = ui_handle.unwrap();
            ui.set_selected_mod_name(SharedString::default());
            ui.set_fuse_mounted(false);
            ui.set_selected_instance(-1);
            refresh_instance_list(&ui, &st.global_settings);
            ui.set_current_page(0);
        });
    }

    // --- Refresh ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_refresh(move || {
            let st = state.borrow();
            let ui = ui_handle.unwrap();
            refresh_ui(&ui, &st);
        });
    }

    // --- Sort Mods ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_sort_mods(move |column| {
            let ui = ui_handle.unwrap();
            let old_col = ui.get_sort_column();
            let ascending = if old_col == column {
                !ui.get_sort_ascending()
            } else {
                true
            };
            ui.set_sort_column(column);
            ui.set_sort_ascending(ascending);

            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let mut entries = build_mod_list(instance, &st.conflict_cache);
            entries.retain(|e| !e.is_overwrite);
            sort_mod_entries(&mut entries, column, ascending);
            apply_collapse_state(&mut entries, &st.collapsed_separators);
            apply_selection_state_to_mod_entries(&ui, &mut entries);
            ui.set_mod_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Reorder Mod (drag-drop / up-down buttons) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_reorder_mod(move |from_index, to_index| {
            if from_index == to_index || from_index < 0 || to_index < 0 {
                return;
            }
            let ui = ui_handle.unwrap();
            let model = ui.get_mod_list();
            let from_idx = from_index as usize;
            let to_idx = to_index as usize;
            if from_idx >= model.row_count() || to_idx >= model.row_count() {
                return;
            }

            let from_entry = model.row_data(from_idx).unwrap();
            let to_entry = model.row_data(to_idx).unwrap();
            let from_name = from_entry.name.to_string();
            let to_name = to_entry.name.to_string();

            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };
            let Some(ref mut profile) = instance.active_profile else {
                return;
            };

            // Build current priority order (ascending)
            let mut sorted: Vec<String> = {
                let mut s: Vec<_> = profile.modlist.entries.iter().collect();
                s.sort_by_key(|e| e.priority);
                s.iter().map(|e| e.name.clone()).collect()
            };

            // Remove from_name from current position
            let Some(from_pos) = sorted.iter().position(|n| n == &from_name) else {
                return;
            };
            sorted.remove(from_pos);

            // Find to_name's position after removal
            let Some(to_pos) = sorted.iter().position(|n| n == &to_name) else {
                return;
            };

            // Insert: if dragging down, insert after target; if up, insert before
            if to_index > from_index {
                sorted.insert(to_pos + 1, from_name.clone());
            } else {
                sorted.insert(to_pos, from_name.clone());
            }

            // Reassign priorities
            let name_refs: Vec<&str> = sorted.iter().map(|s| s.as_str()).collect();
            profile.modlist.reorder(&name_refs);
            save_active_modlist(instance);

            // Reload to pick up new priorities
            if let Ok(reloaded) = Instance::load(&instance.root) {
                st.instance = Some(reloaded);
            }
            refresh_conflict_cache(&mut st);
            refresh_ui(&ui, &st);
            rebuild_vfs_if_mounted(&mut st);

            // Restore selection to the moved mod
            ui.set_selected_mod_name(SharedString::from(from_name.as_str()));
            // Find new index of the moved mod in the refreshed model
            let new_model = ui.get_mod_list();
            let new_idx = (0..new_model.row_count())
                .find(|&i| new_model.row_data(i).unwrap().name == from_name.as_str())
                .map(|i| i as i32)
                .unwrap_or(-1);
            ui.set_selected_mod_index(new_idx);
        });
    }

    // --- Filter Mods ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_filter_mods(move |filter_text| {
            let ui = ui_handle.unwrap();
            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let mut entries = build_mod_list(instance, &st.conflict_cache);
            // Remove overwrite from the model — shown separately
            entries.retain(|e| !e.is_overwrite);

            // Apply current sort
            let column = ui.get_sort_column();
            let ascending = ui.get_sort_ascending();
            sort_mod_entries(&mut entries, column, ascending);

            // Filter by display name (case-insensitive)
            let filter = filter_text.to_string().to_lowercase();
            if !filter.is_empty() {
                entries.retain(|e| {
                    e.display_name.to_lowercase().contains(&filter) || e.is_separator
                    // always show separators for context
                });
            }

            apply_collapse_state(&mut entries, &st.collapsed_separators);
            apply_selection_state_to_mod_entries(&ui, &mut entries);
            ui.set_mod_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Toggle Plugin ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_toggle_plugin(move |plugin_name| {
            let mut st = state.borrow_mut();
            if let Some(ref mut instance) = st.instance {
                let plist = instance.build_plugin_list();
                let plugin_name = plugin_name.to_string();
                let Some(plugin) = plist.find(&plugin_name) else {
                    tracing::warn!("toggle_plugin: plugin '{}' not found", plugin_name);
                    return;
                };
                let new_enabled = !plugin.enabled;

                if let Some(ref mut profile) = instance.active_profile {
                    profile.plugins.set_enabled(&plugin_name, new_enabled);
                    // Persist plugins.txt
                    let plugins_path = profile.path.join("plugins.txt");
                    if let Err(e) = profile.plugins.write(&plugins_path) {
                        tracing::error!("Failed to save plugins.txt: {e}");
                    }
                }

                let ui = ui_handle.unwrap();
                refresh_ui(&ui, &st);
            }
        });
    }

    // --- Sort Plugins ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_sort_plugins(move |column| {
            let ui = ui_handle.unwrap();
            let old_col = ui.get_plugin_sort_column();
            let ascending = if old_col == column {
                !ui.get_plugin_sort_ascending()
            } else {
                true
            };
            ui.set_plugin_sort_column(column);
            ui.set_plugin_sort_ascending(ascending);

            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let plist = instance.build_plugin_list();
            let mut entries = build_plugin_list_entries(&plist);
            sort_plugin_entries(&mut entries, column, ascending);
            ui.set_plugin_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Filter Plugins ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_filter_plugins(move |filter_text| {
            let ui = ui_handle.unwrap();
            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let plist = instance.build_plugin_list();
            let mut entries = build_plugin_list_entries(&plist);

            // Apply current sort
            let column = ui.get_plugin_sort_column();
            let ascending = ui.get_plugin_sort_ascending();
            sort_plugin_entries(&mut entries, column, ascending);

            // Filter by filename (case-insensitive)
            let filter = filter_text.to_string().to_lowercase();
            if !filter.is_empty() {
                entries.retain(|e| {
                    e.filename.to_lowercase().contains(&filter)
                        || e.origin_mod.to_lowercase().contains(&filter)
                });
            }

            ui.set_plugin_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Menu Action ---
    {
        let ui_handle = ui.as_weak();
        ui.on_menu_action(move |action| {
            let ui = ui_handle.unwrap();
            match action.as_str() {
                "import" => ui.invoke_import_instance(),
                "refresh" => ui.invoke_refresh(),
                "quit" => {
                    // Close the window
                    let _ = slint::quit_event_loop();
                }
                "executables" => ui.invoke_open_tool_editor(),
                "settings" => ui.invoke_open_settings(),
                "toggle-filters" => {
                    let current = ui.get_show_filter_panel();
                    ui.set_show_filter_panel(!current);
                }
                "about" => {
                    tracing::info!("MO2 Linux - Mod Organizer 2 for Linux (Slint/Rust)");
                }
                _ => {
                    tracing::warn!("Unknown menu action: {action}");
                }
            }
        });
    }

    // --- Toggle Filter ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_toggle_filter(move |index| {
            let ui = ui_handle.unwrap();
            let model = ui.get_filter_items();
            let idx = index as usize;
            if idx >= model.row_count() {
                return;
            }
            let mut item = model.row_data(idx).unwrap();
            item.checked = !item.checked;
            model.set_row_data(idx, item);

            // Re-filter the mod list
            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let filter_items = ui.get_filter_items();
            let and_mode = ui.get_filter_and_mode();
            let mut entries = build_mod_list(instance, &st.conflict_cache);
            apply_category_filter(&mut entries, &filter_items, and_mode);
            let column = ui.get_sort_column();
            let ascending = ui.get_sort_ascending();
            sort_mod_entries(&mut entries, column, ascending);
            apply_collapse_state(&mut entries, &st.collapsed_separators);
            apply_selection_state_to_mod_entries(&ui, &mut entries);
            ui.set_mod_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Toggle Filter Expand ---
    {
        let ui_handle = ui.as_weak();
        ui.on_toggle_filter_expand(move |index| {
            let ui = ui_handle.unwrap();
            let model = ui.get_filter_items();
            let idx = index as usize;
            if idx >= model.row_count() {
                return;
            }
            let mut item = model.row_data(idx).unwrap();
            item.is_expanded = !item.is_expanded;
            model.set_row_data(idx, item);
            // Note: in a tree view, toggling expand should show/hide children.
            // For now, all items are always visible (flat list with depth indentation).
        });
    }

    // --- Clear Filters ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_clear_filters(move || {
            let ui = ui_handle.unwrap();
            let model = ui.get_filter_items();
            for i in 0..model.row_count() {
                let mut item = model.row_data(i).unwrap();
                if item.checked {
                    item.checked = false;
                    model.set_row_data(i, item);
                }
            }
            // Refresh unfiltered mod list
            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let mut entries = build_mod_list(instance, &st.conflict_cache);
            let column = ui.get_sort_column();
            let ascending = ui.get_sort_ascending();
            sort_mod_entries(&mut entries, column, ascending);
            apply_collapse_state(&mut entries, &st.collapsed_separators);
            apply_selection_state_to_mod_entries(&ui, &mut entries);
            ui.set_mod_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Filter Archives ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_filter_archives(move |filter_text| {
            let ui = ui_handle.unwrap();
            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let archives = instance.scan_archives();
            let filter = filter_text.to_string().to_lowercase();
            let entries: Vec<ArchiveEntry> = archives
                .iter()
                .filter(|(filename, origin, _)| {
                    filter.is_empty()
                        || filename.to_lowercase().contains(&filter)
                        || origin.to_lowercase().contains(&filter)
                })
                .map(|(filename, origin, enabled)| ArchiveEntry {
                    filename: SharedString::from(filename.as_str()),
                    origin_mod: SharedString::from(origin.as_str()),
                    mod_enabled: *enabled,
                })
                .collect();
            ui.set_archive_count(entries.len() as i32);
            ui.set_archive_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Toggle Data Expand ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_toggle_data_expand(move |index| {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();

            // Get the virtual path from the current data list
            let data_list = ui.get_data_list();
            let idx = index as usize;
            if idx >= data_list.row_count() {
                return;
            }
            let item = data_list.row_data(idx).unwrap();
            let vpath = item.virtual_path.to_string();

            // Toggle expanded state
            if st.data_expanded.contains(&vpath) {
                st.data_expanded.remove(&vpath);
            } else {
                st.data_expanded.insert(vpath);
            }

            // Rebuild the display list
            let Some(ref instance) = st.instance else {
                return;
            };
            let entries = build_data_entries(instance, &st.vfs_tree, &st.data_expanded);
            ui.set_data_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Filter Data ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_filter_data(move |filter_text| {
            let ui = ui_handle.unwrap();
            let st = state.borrow();
            let Some(ref instance) = st.instance else {
                return;
            };
            let mut entries = build_data_entries(instance, &st.vfs_tree, &st.data_expanded);
            let filter = filter_text.to_string().to_lowercase();
            if !filter.is_empty() {
                entries.retain(|e| {
                    e.name.to_lowercase().contains(&filter)
                        || e.origin.to_lowercase().contains(&filter)
                });
            }
            ui.set_data_list(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Import Instance ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_import_instance(move || {
            let Some(source) = rfd::FileDialog::new()
                .set_title("Select existing MO2 instance folder")
                .pick_folder()
            else {
                return;
            };

            // Verify source has ModOrganizer.ini
            if !source.join("ModOrganizer.ini").exists() {
                tracing::error!("No ModOrganizer.ini found in {:?}", source);
                return;
            }

            tracing::info!("Using existing MO2 instance directly: {:?}", source);

            // Load selected folder directly (no wrapper instance creation)
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            if let Ok(()) = load_instance(&ui, &mut st, &source) {
                ui.set_current_page(1);
            }
            refresh_instance_list(&ui, &st.global_settings);
        });
    }

    // =====================================================
    // Tool Editor callbacks
    // =====================================================

    // --- Select Tool ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_select_tool(move |index| {
            let ui = ui_handle.unwrap();
            ui.set_selected_tool(index);
            let st = state.borrow();
            let idx = index as usize;
            if idx < st.executables.executables.len() {
                let exe = &st.executables.executables[idx];
                ui.set_tool_edit_title(SharedString::from(exe.title.as_str()));
                ui.set_tool_edit_binary(SharedString::from(exe.binary.as_str()));
                ui.set_tool_edit_arguments(SharedString::from(exe.arguments.as_str()));
                ui.set_tool_edit_workdir(SharedString::from(exe.working_directory.as_str()));
                ui.set_tool_edit_toolbar(exe.show_in_toolbar);
                ui.set_tool_edit_hide(exe.hide);
            }
        });
    }

    // --- Add Tool ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_add_tool(move || {
            let ui = ui_handle.unwrap();
            let title = ui.get_tool_edit_title().to_string();
            let binary = ui.get_tool_edit_binary().to_string();
            if title.is_empty() || binary.is_empty() {
                tracing::warn!("Tool title and binary are required");
                return;
            }
            let exe = Executable {
                title,
                binary,
                arguments: ui.get_tool_edit_arguments().to_string(),
                working_directory: ui.get_tool_edit_workdir().to_string(),
                show_in_toolbar: ui.get_tool_edit_toolbar(),
                hide: ui.get_tool_edit_hide(),
                ..Default::default()
            };
            let mut st = state.borrow_mut();
            st.executables.add(exe);
            refresh_tool_list(&ui, &st);
        });
    }

    // --- Remove Tool ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_remove_tool(move |index| {
            let mut st = state.borrow_mut();
            let idx = index as usize;
            if st.executables.remove(idx).is_some() {
                let ui = ui_handle.unwrap();
                ui.set_selected_tool(-1);
                refresh_tool_list(&ui, &st);
            }
        });
    }

    // --- Save Tools ---
    {
        let state = state.clone();
        let ui_handle = ui.as_weak();
        ui.on_save_tools(move || {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            if st.instance.is_none() {
                return;
            }

            // Sync currently-selected tool's UI fields back to the executables list
            let sel = ui.get_selected_tool() as usize;
            if sel < st.executables.executables.len() {
                let exe = &mut st.executables.executables[sel];
                exe.title = ui.get_tool_edit_title().to_string();
                exe.binary = ui.get_tool_edit_binary().to_string();
                exe.arguments = ui.get_tool_edit_arguments().to_string();
                exe.working_directory = ui.get_tool_edit_workdir().to_string();
                exe.show_in_toolbar = ui.get_tool_edit_toolbar();
                exe.hide = ui.get_tool_edit_hide();
            }

            let mut ini = st.instance.as_ref().unwrap().config.ini.clone();
            st.executables.write_to_ini(&mut ini);
            let instance = st.instance.as_mut().unwrap();
            let ini_path = instance.root.join("ModOrganizer.ini");
            if let Err(e) = ini.write(&ini_path) {
                tracing::error!("Failed to save executables: {e}");
            } else {
                tracing::info!("Executables saved to {:?}", ini_path);
                instance.config.ini = ini;
            }
            refresh_tool_list(&ui, &st);
        });
    }

    // --- Open Tool Editor ---
    {
        let ui_handle = ui.as_weak();
        ui.on_open_tool_editor(move || {
            let ui = ui_handle.unwrap();
            ui.set_show_tool_editor(true);
        });
    }

    // --- Close Tool Editor (cancel: reload from INI, close) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_close_tool_editor(move || {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            // Reload executables from INI
            if let Some(ref instance) = st.instance {
                st.executables = ExecutablesList::read_from_ini_with_prefix(
                    &instance.config.ini,
                    instance.config.wine_prefix_path().as_deref(),
                );
            }
            refresh_tool_list(&ui, &st);
            ui.set_selected_tool(-1);
            ui.set_show_tool_editor(false);
        });
    }

    // =====================================================
    // Settings callbacks
    // =====================================================

    // --- Open Settings ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_open_settings(move || {
            let ui = ui_handle.unwrap();
            let st = state.borrow();
            ui.set_settings_local_inis(true);
            ui.set_settings_local_saves(false);
            ui.set_settings_auto_archive_invalidation(true);
            if let Some(ref instance) = st.instance {
                if let Some(ref profile) = instance.active_profile {
                    ui.set_settings_local_inis(profile.settings.local_settings());
                    ui.set_settings_local_saves(profile.settings.local_saves());
                    ui.set_settings_auto_archive_invalidation(
                        profile.settings.automatic_archive_invalidation(),
                    );
                }
            }
            ui.set_settings_tab(0);
            ui.set_show_settings(true);
        });
    }

    // --- Close Settings ---
    {
        let ui_handle = ui.as_weak();
        ui.on_close_settings(move || {
            let ui = ui_handle.unwrap();
            ui.set_show_settings(false);
        });
    }

    // --- Browse Game Path (managed game exe from settings) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_browse_game_path(move || {
            let Some(file) = rfd::FileDialog::new()
                .add_filter("Executables", &["exe", "sh", ""])
                .pick_file()
            else {
                return;
            };
            // Store the parent directory as gamePath in INI (MO2 convention),
            // but display the full exe path in the UI
            let mut st = state.borrow_mut();
            if let Some(ref mut instance) = st.instance {
                if let Some(parent) = file.parent() {
                    instance.config.set_game_directory(parent);
                }
                let ini_path = instance.root.join("ModOrganizer.ini");
                if let Err(e) = instance.config.write(&ini_path) {
                    tracing::error!("Failed to save game path: {e}");
                } else {
                    tracing::info!("Game path updated to {:?}", file);
                }
                let ui = ui_handle.unwrap();
                ui.set_game_path(file.display().to_string().into());
            }
        });
    }

    // --- Browse Settings Base Dir ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_settings_base_dir(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            let path_str = folder.display().to_string();
            ui.set_settings_base_dir(SharedString::from(path_str.as_str()));
            // Update derived paths
            ui.set_settings_downloads_dir(folder.join("downloads").display().to_string().into());
            ui.set_settings_mods_dir(folder.join("mods").display().to_string().into());
            ui.set_settings_profiles_dir(folder.join("profiles").display().to_string().into());
            ui.set_settings_overwrite_dir(folder.join("overwrite").display().to_string().into());
        });
    }

    // --- Browse Settings Downloads ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_settings_downloads(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_settings_downloads_dir(folder.display().to_string().into());
        });
    }

    // --- Browse Settings Mods ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_settings_mods(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_settings_mods_dir(folder.display().to_string().into());
        });
    }

    // --- Browse Settings Profiles ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_settings_profiles(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_settings_profiles_dir(folder.display().to_string().into());
        });
    }

    // --- Browse Settings Overwrite ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_settings_overwrite(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_settings_overwrite_dir(folder.display().to_string().into());
        });
    }

    // --- Profile settings: Local INIs ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_set_profile_local_inis(move |enabled| {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };
            let Some(ref mut profile) = instance.active_profile else {
                return;
            };
            profile.settings.set_local_settings(enabled);
            let settings_path = profile.path.join("settings.ini");
            if let Err(e) = profile.settings.write(&settings_path) {
                tracing::error!("Failed to save profile settings.ini: {e}");
                return;
            }
            ui.set_settings_local_inis(enabled);
        });
    }

    // --- Profile settings: Local Saves ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_set_profile_local_saves(move |enabled| {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };
            let Some(ref mut profile) = instance.active_profile else {
                return;
            };
            profile.settings.set_local_saves(enabled);
            let settings_path = profile.path.join("settings.ini");
            if let Err(e) = profile.settings.write(&settings_path) {
                tracing::error!("Failed to save profile settings.ini: {e}");
                return;
            }
            ui.set_settings_local_saves(enabled);
            refresh_ui(&ui, &st);
        });
    }

    // --- Profile settings: Automatic Archive Invalidation ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_set_profile_auto_archive_invalidation(move |enabled| {
            let ui = ui_handle.unwrap();
            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };
            let Some(ref mut profile) = instance.active_profile else {
                return;
            };
            profile.settings.set_automatic_archive_invalidation(enabled);
            let settings_path = profile.path.join("settings.ini");
            if let Err(e) = profile.settings.write(&settings_path) {
                tracing::error!("Failed to save profile settings.ini: {e}");
                return;
            }
            ui.set_settings_auto_archive_invalidation(enabled);
        });
    }

    // =====================================================
    // Wine/Proton callbacks
    // =====================================================

    // --- Detect Games ---
    {
        let ui_handle = ui.as_weak();
        ui.on_detect_games(move || {
            let result = nak_rust::game_finder::detect_all_games();
            let ui = ui_handle.unwrap();
            let games: Vec<DetectedGame> = result
                .games
                .iter()
                .map(|g| DetectedGame {
                    name: SharedString::from(g.name.as_str()),
                    app_id: SharedString::from(g.app_id.as_str()),
                    install_path: SharedString::from(g.install_path.display().to_string().as_str()),
                    prefix_path: SharedString::from(
                        g.prefix_path
                            .as_ref()
                            .map(|p: &PathBuf| p.display().to_string())
                            .unwrap_or_default()
                            .as_str(),
                    ),
                    launcher: SharedString::from(g.launcher.display_name()),
                })
                .collect();
            tracing::info!("Detected {} games", games.len());
            ui.set_detected_games(ModelRc::new(VecModel::from(games)));
        });
    }

    // --- Select Detected Game (in wizard) ---
    {
        let ui_handle = ui.as_weak();
        ui.on_select_detected_game(move |index| {
            let ui = ui_handle.unwrap();
            let model = ui.get_detected_games();
            let idx = index as usize;
            if idx >= model.row_count() {
                return;
            }
            let game = model.row_data(idx).unwrap();
            ui.set_wizard_game_name(game.name.clone());
            // Auto-fill game path if available
            if !game.install_path.is_empty() {
                ui.set_wizard_game_path(game.install_path.clone());
            }
        });
    }

    // --- Set Proton Version (without recreating prefix) ---
    {
        let ui_handle = ui.as_weak();
        ui.on_set_proton_version(move |proton_name| {
            let ui = ui_handle.unwrap();
            let proton_name = proton_name.to_string();
            if proton_name.is_empty() {
                return;
            }

            // Resolve proton name to path from the loaded model
            let proton_model = ui.get_available_protons();
            let proton_path = (0..proton_model.row_count())
                .filter_map(|i| proton_model.row_data(i))
                .find(|p| p.name.as_str() == proton_name)
                .map(|p| p.path.to_string())
                .unwrap_or_default();
            if proton_path.is_empty() {
                tracing::warn!("Could not resolve Proton path for selection: {proton_name}");
                return;
            }

            ui.set_selected_proton_path(SharedString::from(proton_path.as_str()));

            // If a Fluorine prefix exists, persist the new Proton immediately.
            if let Some(mut config) =
                mo2core::fluorine::FluorineConfig::load().filter(|c| c.prefix_exists())
            {
                if config.proton_name == proton_name && config.proton_path == proton_path {
                    return;
                }

                config.proton_name = proton_name.clone();
                config.proton_path = proton_path;
                if let Err(e) = config.save() {
                    tracing::error!("Failed to save updated Proton selection: {e}");
                    ui.set_prefix_status_text(SharedString::from("Error saving Proton selection"));
                    return;
                }

                ui.set_prefix_status_text(SharedString::from(
                    format!("AppID: {} | Proton: {}", config.app_id, proton_name).as_str(),
                ));
                tracing::info!(
                    "Updated Fluorine Proton to {proton_name} without recreating prefix"
                );
            }
        });
    }

    // --- Create Fluorine Prefix ---
    {
        let ui_handle = ui.as_weak();
        ui.on_create_prefix(move || {
            let ui = ui_handle.unwrap();
            let proton_name = ui.get_selected_proton_name().to_string();

            if proton_name.is_empty() {
                tracing::warn!("No Proton version selected");
                return;
            }

            // Resolve proton name to path
            let proton_model = ui.get_available_protons();
            let proton_path = (0..proton_model.row_count())
                .filter_map(|i| proton_model.row_data(i))
                .find(|p| p.name.as_str() == proton_name)
                .map(|p| p.path.to_string())
                .unwrap_or_default();

            if proton_path.is_empty() {
                tracing::error!("Could not resolve Proton path for: {proton_name}");
                return;
            }

            // Resolve to SteamProton struct for NaK APIs
            let protons = nak_rust::steam::find_steam_protons();
            let steam_proton = match protons.iter().find(|p| p.name == proton_name) {
                Some(p) => p.clone(),
                None => {
                    tracing::error!("Proton not found in Steam: {proton_name}");
                    ui.set_prefix_status_text(SharedString::from(
                        "Error: Proton version not found",
                    ));
                    return;
                }
            };

            // Mark UI as busy
            ui.set_prefix_busy(true);
            ui.set_prefix_progress(0.0);
            ui.set_prefix_status_text(SharedString::from("Creating Steam shortcut..."));

            // Step 1: Create Steam shortcut (fast, stays on UI thread)
            let result = match nak_rust::steam::add_mod_manager_shortcut(
                "Fluorine Manager",
                "/usr/bin/true", // placeholder exe — prefix is what matters
                "/tmp",
                &proton_name,
                None,
                false,
            ) {
                Ok(r) => r,
                Err(e) => {
                    tracing::error!("Failed to create Steam shortcut: {e}");
                    ui.set_prefix_busy(false);
                    ui.set_prefix_status_text(SharedString::from(
                        format!("Error creating shortcut: {e}").as_str(),
                    ));
                    return;
                }
            };

            // Step 2: Install deps on background thread to avoid blocking UI
            let app_id = result.app_id;
            let prefix_path = result.prefix_path.clone();
            let proton_name_bg = proton_name.clone();
            let proton_path_bg = proton_path.clone();
            let ui_weak = ui.as_weak();

            std::thread::spawn(move || {
                let cancel_flag = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));

                // Wire TaskContext callbacks to push status updates to the UI thread
                let ui_status = ui_weak.clone();
                let ui_progress = ui_weak.clone();
                let ctx = nak_rust::installers::TaskContext::new(
                    move |msg: String| {
                        let ui_w = ui_status.clone();
                        let _ = slint::invoke_from_event_loop(move || {
                            if let Some(ui) = ui_w.upgrade() {
                                ui.set_prefix_status_text(SharedString::from(msg.as_str()));
                            }
                        });
                    },
                    |msg| tracing::debug!("Prefix log: {msg}"),
                    move |progress: f32| {
                        let ui_w = ui_progress.clone();
                        let _ = slint::invoke_from_event_loop(move || {
                            if let Some(ui) = ui_w.upgrade() {
                                ui.set_prefix_progress(progress);
                            }
                        });
                    },
                    cancel_flag,
                );

                // Convert error to String (Send) before crossing thread boundary
                let install_err: Option<String> = nak_rust::installers::install_all_dependencies(
                    &prefix_path,
                    &steam_proton,
                    &ctx,
                    0.0,
                    1.0,
                    app_id,
                )
                .err()
                .map(|e| e.to_string());

                // Back to UI thread for final state updates
                let ui_final = ui_weak.clone();
                let _ = slint::invoke_from_event_loop(move || {
                    let Some(ui) = ui_final.upgrade() else { return };
                    ui.set_prefix_busy(false);

                    if let Some(err) = install_err {
                        tracing::error!("Failed to install prefix dependencies: {err}");
                        ui.set_prefix_status_text(SharedString::from(
                            format!("Error: {err}").as_str(),
                        ));
                        return;
                    }

                    // Save Fluorine config to disk
                    let config = mo2core::fluorine::FluorineConfig {
                        app_id,
                        prefix_path: prefix_path.display().to_string(),
                        proton_name: proton_name_bg.clone(),
                        proton_path: proton_path_bg.clone(),
                        created: chrono::Local::now().to_rfc3339(),
                    };
                    if let Err(e) = config.save() {
                        tracing::error!("Failed to save Fluorine config: {e}");
                        ui.set_prefix_status_text(SharedString::from(
                            format!("Error saving config: {e}").as_str(),
                        ));
                        return;
                    }

                    // Update UI
                    ui.set_prefix_exists(true);
                    ui.set_prefix_progress(1.0);
                    ui.set_prefix_status_text(SharedString::from(
                        format!("AppID: {} | Proton: {}", app_id, proton_name_bg).as_str(),
                    ));
                    ui.set_wine_prefix_path(prefix_path.display().to_string().into());
                    ui.set_steam_app_id(SharedString::from(app_id.to_string().as_str()));
                    ui.set_selected_proton_path(SharedString::from(proton_path_bg.as_str()));

                    tracing::info!(
                        "Fluorine prefix created and initialized: AppID={}, prefix={}",
                        app_id,
                        prefix_path.display()
                    );
                });
            });
        });
    }

    // --- Delete Fluorine Prefix ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_delete_prefix(move || {
            let ui = ui_handle.unwrap();

            if let Some(config) = mo2core::fluorine::FluorineConfig::load() {
                // Remove Steam shortcut
                let app_id = config.app_id;
                if let Err(e) = nak_rust::steam::remove_steam_shortcut(app_id) {
                    tracing::error!("Failed to remove Steam shortcut: {e}");
                }

                // Destroy prefix on disk + config
                if let Err(e) = config.destroy_prefix() {
                    tracing::error!("Failed to destroy Fluorine prefix: {e}");
                    return;
                }

                // Clear instance INI proton settings
                let mut st = state.borrow_mut();
                if let Some(ref mut instance) = st.instance {
                    instance.config.set_proton_path(Path::new(""));
                    instance.config.set_wine_prefix_path(Path::new(""));
                    instance.config.set_steam_app_id(0);
                    let ini_path = instance.root.join("ModOrganizer.ini");
                    let _ = instance.config.write(&ini_path);
                }

                // Update UI
                ui.set_prefix_exists(false);
                ui.set_prefix_status_text(SharedString::from("No prefix configured"));
                ui.set_wine_prefix_path(SharedString::default());
                ui.set_steam_app_id(SharedString::default());

                tracing::info!("Fluorine prefix deleted (AppID: {})", app_id);
            }
        });
    }

    // --- Browse Wine Prefix (kept for manual override) ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_wine_prefix(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_wine_prefix_path(folder.display().to_string().into());
        });
    }

    // --- Browse Tool Binary ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_tool_binary(move || {
            let Some(file) = rfd::FileDialog::new().pick_file() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_tool_edit_binary(file.display().to_string().into());
        });
    }

    // --- Browse Tool Working Directory ---
    {
        let ui_handle = ui.as_weak();
        ui.on_browse_tool_workdir(move || {
            let Some(folder) = rfd::FileDialog::new().pick_folder() else {
                return;
            };
            let ui = ui_handle.unwrap();
            ui.set_tool_edit_workdir(folder.display().to_string().into());
        });
    }

    // Process monitor timer — polls /proc/<pid> to detect when launched process exits.
    let process_timer = Rc::new(Timer::default());

    // --- Play (toolbar executable) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        let timer = process_timer.clone();
        ui.on_play(move || {
            let ui = ui_handle.unwrap();
            let selected = ui.get_selected_toolbar_tool().to_string();
            if selected.is_empty() {
                tracing::warn!("No toolbar tool selected");
                return;
            }

            let mut st = state.borrow_mut();

            // Prevent concurrent launches
            if st.locked_pid.is_some() {
                tracing::warn!("App is locked — a process is already running");
                return;
            }

            // Find the executable by title
            let idx = st
                .executables
                .executables
                .iter()
                .position(|e| e.title == selected);
            let Some(idx) = idx else {
                tracing::error!("Toolbar tool '{}' not found", selected);
                return;
            };

            // Auto-mount VFS before running.
            // Avoid forced remount churn here; it can create transient stale mounts.
            if !is_vfs_mounted(&st) {
                auto_mount_vfs(&mut st);
                ui.set_fuse_mounted(is_vfs_mounted(&st));
            }

            let exe = &st.executables.executables[idx];
            let tool_name = exe.title.clone();
            tracing::info!("Playing: {} ({})", exe.title, exe.binary);

            // Deploy plugins.txt + profile INI/saves to Wine prefix before launch
            deploy_plugins_to_prefix(&st);

            let ini_sync = build_ini_sync_info(&st);
            let save_sync = build_save_sync_info(&st);
            let config = build_launch_config_vfs(exe, &st);
            match mo2core::launcher::process::launch(&config) {
                Ok(child) => {
                    let pid = child.id();
                    tracing::info!("Tool '{}' launched (pid: {})", tool_name, pid);
                    st.locked_pid = Some(pid);
                    ui.set_locked(true);
                    ui.set_locked_tool_name(SharedString::from(tool_name.as_str()));
                    start_process_monitor(&timer, &ui_handle, &state, pid, ini_sync, save_sync);
                }
                Err(e) => {
                    tracing::error!("Failed to launch '{}': {e}", tool_name);
                }
            }
        });
    }

    // --- Run Tool (auto-mounts VFS first) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        let timer = process_timer.clone();
        ui.on_run_tool(move |index| {
            let mut st = state.borrow_mut();
            let idx = index as usize;
            if idx >= st.executables.executables.len() {
                return;
            }

            // Prevent concurrent launches
            if st.locked_pid.is_some() {
                tracing::warn!("App is locked — a process is already running");
                return;
            }

            // Auto-mount VFS before running.
            if !is_vfs_mounted(&st) {
                auto_mount_vfs(&mut st);
                let ui = ui_handle.unwrap();
                ui.set_fuse_mounted(is_vfs_mounted(&st));
            }

            let exe = &st.executables.executables[idx];
            let tool_name = exe.title.clone();
            tracing::info!("Launching tool: {} ({})", exe.title, exe.binary);

            // Deploy plugins.txt + profile INI/saves to Wine prefix before launch
            deploy_plugins_to_prefix(&st);

            let ini_sync = build_ini_sync_info(&st);
            let save_sync = build_save_sync_info(&st);
            let config = build_launch_config_vfs(exe, &st);
            match mo2core::launcher::process::launch(&config) {
                Ok(child) => {
                    let pid = child.id();
                    tracing::info!("Tool '{}' launched (pid: {})", tool_name, pid);
                    st.locked_pid = Some(pid);
                    let ui = ui_handle.unwrap();
                    ui.set_locked(true);
                    ui.set_locked_tool_name(SharedString::from(tool_name.as_str()));
                    start_process_monitor(&timer, &ui_handle, &state, pid, ini_sync, save_sync);
                }
                Err(e) => {
                    tracing::error!("Failed to launch '{}': {e}", tool_name);
                }
            }
        });
    }

    // --- Force Unlock ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        let timer = process_timer.clone();
        ui.on_force_unlock(move || {
            let mut st = state.borrow_mut();
            if let Some(pid) = st.locked_pid.take() {
                tracing::warn!("Force unlocking (pid {} may still be running)", pid);
            }
            timer.stop();
            let ui = ui_handle.unwrap();
            ui.set_locked(false);
            ui.set_locked_tool_name(SharedString::default());
        });
    }

    // --- Mod Context Menu Action ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_mod_context_action(move |action| {
            let ui = ui_handle.unwrap();
            let mod_name = ui.get_selected_mod_name().to_string();
            if mod_name.is_empty() {
                return;
            }
            let action = action.to_string();
            match action.as_str() {
                "install-mod" => {
                    let archive = rfd::FileDialog::new()
                        .set_title("Select Mod Archive")
                        .add_filter(
                            "Archives",
                            &[
                                "zip", "7z", "rar", "tar", "gz", "xz", "bz2", "tgz", "tbz2", "txz",
                            ],
                        )
                        .pick_file();
                    let Some(archive_path) = archive else {
                        return;
                    };
                    open_install_dialog_for_archive(&ui, &state, &archive_path);
                }
                "create-separator" => {
                    let mut st = state.borrow_mut();
                    let Some(ref mut instance) = st.instance else {
                        return;
                    };
                    let Some(ref mut profile) = instance.active_profile else {
                        return;
                    };

                    let existing: HashSet<String> = profile
                        .modlist
                        .entries
                        .iter()
                        .map(|e| e.name.to_lowercase())
                        .collect();
                    let mut idx = 1;
                    let sep_name = loop {
                        let candidate = build_separator_name_for_profile(
                            &format!("New Separator {}", idx),
                            &profile.modlist.entries,
                        );
                        if !existing.contains(&candidate.to_lowercase()) {
                            break candidate;
                        }
                        idx += 1;
                    };
                    let max_priority = profile
                        .modlist
                        .entries
                        .iter()
                        .map(|e| e.priority)
                        .max()
                        .unwrap_or(-1);
                    profile
                        .modlist
                        .entries
                        .push(mo2core::config::modlist::ModListEntry {
                            name: sep_name.clone(),
                            status: mo2core::config::modlist::ModStatus::Enabled,
                            priority: max_priority + 1,
                        });
                    save_active_modlist(instance);
                    if let Ok(reloaded) = Instance::load(&instance.root) {
                        st.instance = Some(reloaded);
                    }
                    refresh_conflict_cache(&mut st);
                    refresh_ui(&ui, &st);
                    ui.set_selected_mod_name(SharedString::from(sep_name.as_str()));
                    rebuild_vfs_if_mounted(&mut st);
                }
                "rename" => {
                    // Show rename dialog for regular mod
                    let display = ModList::display_name(&mod_name);
                    ui.set_rename_dialog_title(SharedString::from("Rename Mod"));
                    ui.set_rename_dialog_value(SharedString::from(display.trim()));
                    ui.set_rename_target_name(SharedString::from(mod_name.as_str()));
                    ui.set_rename_target_is_separator(false);
                    ui.set_show_rename_dialog(true);
                }
                "toggle" => {
                    let mut st = state.borrow_mut();
                    let Some(ref mut instance) = st.instance else {
                        return;
                    };
                    let Some(idx) = instance.mods.iter().position(|m| m.name == mod_name) else {
                        return;
                    };
                    let new_enabled = !instance.mods[idx].enabled;
                    if let Some(ref mut profile) = instance.active_profile {
                        profile.set_mod_enabled(&mod_name, new_enabled);
                        if let Some(p) = profile.mod_priority(&mod_name) {
                            instance.mods[idx].priority = p;
                        }
                    }
                    instance.mods[idx].enabled = new_enabled;
                    save_active_modlist(instance);
                    refresh_conflict_cache(&mut st);
                    refresh_ui(&ui, &st);
                    rebuild_vfs_if_mounted(&mut st);
                }
                "open-folder" => {
                    let st = state.borrow();
                    let Some(ref instance) = st.instance else {
                        return;
                    };
                    let mod_path = instance.mods_dir().join(&mod_name);
                    if mod_path.is_dir() {
                        let _ = std::process::Command::new("xdg-open")
                            .arg(&mod_path)
                            .spawn();
                    }
                }
                "delete" => {
                    let mut st = state.borrow_mut();
                    let Some(ref mut instance) = st.instance else {
                        return;
                    };
                    // Remove from modlist
                    if let Some(ref mut profile) = instance.active_profile {
                        profile.modlist.entries.retain(|e| e.name != mod_name);
                    }
                    // Remove mod folder from disk
                    let mod_path = instance.mods_dir().join(&mod_name);
                    if mod_path.is_dir() {
                        if let Err(e) = std::fs::remove_dir_all(&mod_path) {
                            tracing::error!("Failed to delete mod '{}': {e}", mod_name);
                            return;
                        }
                    }
                    save_active_modlist(instance);
                    if let Ok(reloaded) = Instance::load(&instance.root) {
                        st.instance = Some(reloaded);
                    }
                    refresh_conflict_cache(&mut st);
                    refresh_ui(&ui, &st);
                    ui.set_selected_mod_name(SharedString::default());
                    rebuild_vfs_if_mounted(&mut st);
                }
                "rename-sep" => {
                    // Show rename dialog for separator
                    let display = ModList::display_name(&mod_name);
                    ui.set_rename_dialog_title(SharedString::from("Rename Separator"));
                    ui.set_rename_dialog_value(SharedString::from(display.trim()));
                    ui.set_rename_target_name(SharedString::from(mod_name.as_str()));
                    ui.set_rename_target_is_separator(true);
                    ui.set_show_rename_dialog(true);
                }
                "delete-sep" => {
                    let mut st = state.borrow_mut();
                    let Some(ref mut instance) = st.instance else {
                        return;
                    };
                    let Some(ref mut profile) = instance.active_profile else {
                        return;
                    };
                    if !ModList::is_separator(&mod_name) {
                        return;
                    }
                    let before = profile.modlist.entries.len();
                    profile.modlist.entries.retain(|e| e.name != mod_name);
                    if profile.modlist.entries.len() != before {
                        save_active_modlist(instance);
                        if let Ok(reloaded) = Instance::load(&instance.root) {
                            st.instance = Some(reloaded);
                        }
                        refresh_conflict_cache(&mut st);
                        refresh_ui(&ui, &st);
                        ui.set_selected_mod_name(SharedString::default());
                        rebuild_vfs_if_mounted(&mut st);
                    }
                }
                _ => {
                    tracing::warn!("Unknown mod context action: {action}");
                }
            }
        });
    }

    // --- Plugin Context Menu Action ---
    {
        let ui_handle = ui.as_weak();
        ui.on_plugin_context_action(move |action| {
            let ui = ui_handle.unwrap();
            let plugin_name = ui.get_context_plugin_name().to_string();
            if plugin_name.is_empty() {
                return;
            }
            match action.to_string().as_str() {
                "toggle" => {
                    ui.invoke_toggle_plugin(SharedString::from(plugin_name.as_str()));
                }
                other => {
                    tracing::warn!("Unknown plugin context action: {other}");
                }
            }
        });
    }

    // --- Confirm Rename (from rename dialog) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_confirm_rename(move |new_name| {
            let ui = ui_handle.unwrap();
            let new_name = new_name.to_string().trim().to_string();
            if new_name.is_empty() {
                return;
            }
            let old_name = ui.get_rename_target_name().to_string();
            let is_separator = ui.get_rename_target_is_separator();

            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };

            if is_separator {
                // Rename separator in modlist
                let Some(ref mut profile) = instance.active_profile else {
                    return;
                };
                let new_entry_name =
                    build_separator_name_for_profile(&new_name, &profile.modlist.entries);
                if let Some(entry) = profile
                    .modlist
                    .entries
                    .iter_mut()
                    .find(|e| e.name == old_name)
                {
                    entry.name = new_entry_name.clone();
                }
                save_active_modlist(instance);
                if let Ok(reloaded) = Instance::load(&instance.root) {
                    st.instance = Some(reloaded);
                }
                refresh_conflict_cache(&mut st);
                refresh_ui(&ui, &st);
                ui.set_selected_mod_name(SharedString::from(new_entry_name.as_str()));
                rebuild_vfs_if_mounted(&mut st);
            } else {
                // Rename mod folder on disk + update modlist
                let mods_dir = instance.mods_dir();
                let old_path = mods_dir.join(&old_name);
                let new_path = mods_dir.join(&new_name);

                if new_path.exists() {
                    tracing::error!("Cannot rename: '{}' already exists", new_name);
                    let _ = rfd::MessageDialog::new()
                        .set_title("Rename Failed")
                        .set_level(rfd::MessageLevel::Error)
                        .set_description(format!("A mod named '{}' already exists.", new_name))
                        .set_buttons(rfd::MessageButtons::Ok)
                        .show();
                    return;
                }

                if old_path.is_dir() {
                    if let Err(e) = std::fs::rename(&old_path, &new_path) {
                        tracing::error!("Failed to rename mod folder: {e}");
                        return;
                    }
                }

                // Update modlist entry
                if let Some(ref mut profile) = instance.active_profile {
                    if let Some(entry) = profile
                        .modlist
                        .entries
                        .iter_mut()
                        .find(|e| e.name == old_name)
                    {
                        entry.name = new_name.clone();
                    }
                    save_active_modlist(instance);
                }

                if let Ok(reloaded) = Instance::load(&instance.root) {
                    st.instance = Some(reloaded);
                }
                refresh_conflict_cache(&mut st);
                refresh_ui(&ui, &st);
                ui.set_selected_mod_name(SharedString::from(new_name.as_str()));
                rebuild_vfs_if_mounted(&mut st);
            }
        });
    }

    // --- Plugin Reorder (drag-drop) ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_reorder_plugin(move |from_index, to_index| {
            if from_index == to_index || from_index < 0 || to_index < 0 {
                return;
            }
            let ui = ui_handle.unwrap();
            let model = ui.get_plugin_list();
            let from_idx = from_index as usize;
            let to_idx = to_index as usize;
            if from_idx >= model.row_count() || to_idx >= model.row_count() {
                return;
            }

            let from_entry = model.row_data(from_idx).unwrap();
            let to_entry = model.row_data(to_idx).unwrap();
            let from_name = from_entry.filename.to_string();
            let to_name = to_entry.filename.to_string();

            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };
            let Some(ref mut profile) = instance.active_profile else {
                return;
            };

            // Build current load order (priority-sorted list)
            let mut sorted: Vec<String> = profile.load_order.plugins.clone();
            if sorted.is_empty() {
                // Fall back to plugins.txt order
                sorted = profile
                    .plugins
                    .entries
                    .iter()
                    .map(|e| e.filename.clone())
                    .collect();
            }

            // Remove from_name from current position
            let Some(from_pos) = sorted.iter().position(|n| n == &from_name) else {
                return;
            };
            sorted.remove(from_pos);

            // Find to_name's position after removal
            let Some(to_pos) = sorted.iter().position(|n| n == &to_name) else {
                return;
            };

            // Insert: if dragging down, insert after target; if up, insert before
            if to_index > from_index {
                sorted.insert(to_pos + 1, from_name.clone());
            } else {
                sorted.insert(to_pos, from_name.clone());
            }

            // Update load order
            profile.load_order.plugins = sorted.clone();
            let lo_path = profile.path.join("loadorder.txt");
            if let Err(e) = profile.load_order.write(&lo_path) {
                tracing::error!("Failed to save loadorder.txt: {e}");
            }

            // Update plugins.txt to match new order
            let new_entries: Vec<mo2core::config::plugins::PluginEntry> = sorted
                .iter()
                .filter_map(|name| {
                    profile
                        .plugins
                        .entries
                        .iter()
                        .find(|e| e.filename == *name)
                        .cloned()
                })
                .collect();
            // Keep any plugins not in load_order at the end
            let mut remaining: Vec<mo2core::config::plugins::PluginEntry> = profile
                .plugins
                .entries
                .iter()
                .filter(|e| !sorted.contains(&e.filename))
                .cloned()
                .collect();
            let mut final_entries = new_entries;
            final_entries.append(&mut remaining);
            profile.plugins.entries = final_entries;
            let plugins_path = profile.path.join("plugins.txt");
            if let Err(e) = profile.plugins.write(&plugins_path) {
                tracing::error!("Failed to save plugins.txt: {e}");
            }

            refresh_ui(&ui, &st);
        });
    }

    // --- Install Confirm ---
    {
        let ui_handle = ui.as_weak();
        let state = state.clone();
        ui.on_install_confirm(move |mod_name| {
            let ui = ui_handle.unwrap();
            let mod_name = mod_name.to_string().trim().to_string();
            if mod_name.is_empty() {
                return;
            }

            let staging_path_str = ui.get_install_staging_path().to_string();
            let data_path_str = ui.get_install_data_path().to_string();
            let staging_path = PathBuf::from(&staging_path_str);

            let mut st = state.borrow_mut();
            let Some(ref mut instance) = st.instance else {
                return;
            };

            let mod_dir = instance.mods_dir().join(&mod_name);
            if mod_dir.exists() {
                tracing::error!("Mod '{}' already exists", mod_name);
                let _ = rfd::MessageDialog::new()
                    .set_title("Install Failed")
                    .set_level(rfd::MessageLevel::Error)
                    .set_description(format!("A mod named '{}' already exists.", mod_name))
                    .set_buttons(rfd::MessageButtons::Ok)
                    .show();
                return;
            }

            // Determine source directory.
            // If no data path is selected, unwrap one top-level wrapper folder (common archive layout).
            let source_dir = if data_path_str.is_empty() {
                detect_single_wrapper_dir(&staging_path).unwrap_or_else(|| staging_path.clone())
            } else {
                staging_path.join(&data_path_str)
            };

            // Copy files to mod directory
            if let Err(e) = std::fs::create_dir_all(&mod_dir) {
                tracing::error!("Failed to create mod dir: {e}");
                return;
            }
            match copy_dir_recursive_install(&source_dir, &mod_dir) {
                Ok(count) => {
                    tracing::info!("Installed {} files to '{}'", count, mod_name);
                }
                Err(e) => {
                    tracing::error!("Failed to install mod: {e}");
                    let _ = std::fs::remove_dir_all(&mod_dir);
                    return;
                }
            }

            // Preserve Root/ sibling when Data was selected from within a wrapper layout
            // (e.g. SKSE/Data + SKSE/Root should install as <mod>/Data.. and <mod>/Root..).
            if !data_path_str.is_empty() {
                if let Some(root_src) =
                    find_root_sibling_for_selected_data(&staging_path, &data_path_str)
                {
                    let root_dest = mod_dir.join("Root");
                    if let Err(e) = copy_dir_recursive_install(&root_src, &root_dest) {
                        tracing::error!(
                            "Failed to copy Root folder {:?} -> {:?}: {e}",
                            root_src,
                            root_dest
                        );
                        let _ = std::fs::remove_dir_all(&mod_dir);
                        return;
                    }
                }
            }

            // Add to modlist
            if let Some(ref mut profile) = instance.active_profile {
                let max_priority = profile
                    .modlist
                    .entries
                    .iter()
                    .map(|e| e.priority)
                    .max()
                    .unwrap_or(-1);
                profile
                    .modlist
                    .entries
                    .push(mo2core::config::modlist::ModListEntry {
                        name: mod_name.clone(),
                        status: mo2core::config::modlist::ModStatus::Enabled,
                        priority: max_priority + 1,
                    });
                save_active_modlist(instance);
            }

            // Clean up staging
            let _ = std::fs::remove_dir_all(&staging_path);

            // Reload instance
            if let Ok(reloaded) = Instance::load(&instance.root) {
                st.instance = Some(reloaded);
            }
            refresh_conflict_cache(&mut st);
            refresh_ui(&ui, &st);
            ui.set_selected_mod_name(SharedString::from(mod_name.as_str()));
            rebuild_vfs_if_mounted(&mut st);
            ui.set_show_install_tree_menu(false);
            ui.set_show_install_dialog(false);
        });
    }

    // --- Install Cancel ---
    {
        let ui_handle = ui.as_weak();
        ui.on_install_cancel(move || {
            let ui = ui_handle.unwrap();
            // Clean up staging dir
            let staging_path = ui.get_install_staging_path().to_string();
            if !staging_path.is_empty() {
                let _ = std::fs::remove_dir_all(&staging_path);
            }
            ui.set_show_install_tree_menu(false);
            ui.set_show_install_dialog(false);
        });
    }

    // --- Install Set Data Dir ---
    {
        let ui_handle = ui.as_weak();
        ui.on_install_set_data_dir(move |path| {
            let ui = ui_handle.unwrap();
            let path = path.to_string();
            let staging_path_str = ui.get_install_staging_path().to_string();
            let staging_path = PathBuf::from(&staging_path_str);

            // Validate
            let check_dir = if path.is_empty() {
                staging_path.clone()
            } else {
                staging_path.join(&path)
            };
            let valid = mo2core::install::looks_like_game_data(&check_dir);

            // Update tree to mark new data root
            let model = ui.get_install_tree();
            for i in 0..model.row_count() {
                let mut entry = model.row_data(i).unwrap();
                entry.is_data_root = entry.path == path.as_str();
                model.set_row_data(i, entry);
            }

            ui.set_install_data_path(SharedString::from(path.as_str()));
            ui.set_install_data_valid(valid);
        });
    }

    // --- Install Toggle Expand ---
    {
        let ui_handle = ui.as_weak();
        ui.on_install_toggle_expand(move |path| {
            let ui = ui_handle.unwrap();
            let path = path.to_string();
            let model = ui.get_install_tree();

            // Toggle the entry's expansion in the model
            for i in 0..model.row_count() {
                let entry = model.row_data(i).unwrap();
                if entry.path == path.as_str() {
                    let mut updated = entry.clone();
                    updated.is_expanded = !updated.is_expanded;
                    model.set_row_data(i, updated);
                    break;
                }
            }

            // Rebuild the full tree from staging with updated expanded state
            let staging_path_str = ui.get_install_staging_path().to_string();
            if staging_path_str.is_empty() {
                return;
            }
            // Collect current expanded state
            let mut expanded_paths: HashSet<String> = HashSet::new();
            for i in 0..model.row_count() {
                let entry = model.row_data(i).unwrap();
                if entry.is_expanded {
                    expanded_paths.insert(entry.path.to_string());
                }
            }
            let data_path = ui.get_install_data_path().to_string();
            let staging_path = PathBuf::from(&staging_path_str);
            let entries = build_install_tree_with_state(&staging_path, &expanded_paths, &data_path);
            ui.set_install_tree(ModelRc::new(VecModel::from(entries)));
        });
    }

    // --- Install Create Folder ---
    {
        let ui_handle = ui.as_weak();
        ui.on_install_create_folder(move |parent_path| {
            let ui = ui_handle.unwrap();
            let parent_path = parent_path.to_string();
            let staging_path_str = ui.get_install_staging_path().to_string();
            if staging_path_str.is_empty() {
                return;
            }

            let staging_path = PathBuf::from(&staging_path_str);
            let parent_dir = if parent_path.is_empty() {
                staging_path.clone()
            } else {
                staging_path.join(&parent_path)
            };
            if !parent_dir.is_dir() {
                return;
            }

            let mut idx = 1usize;
            let folder_name = loop {
                let candidate = if idx == 1 {
                    "New Folder".to_string()
                } else {
                    format!("New Folder {idx}")
                };
                if !parent_dir.join(&candidate).exists() {
                    break candidate;
                }
                idx += 1;
            };

            if let Err(e) = std::fs::create_dir_all(parent_dir.join(&folder_name)) {
                tracing::error!("Failed to create folder in install staging: {e}");
                return;
            }

            // Keep current expansion state and ensure parent stays expanded.
            let model = ui.get_install_tree();
            let mut expanded_paths: HashSet<String> = HashSet::new();
            for i in 0..model.row_count() {
                let entry = model.row_data(i).unwrap();
                if entry.is_expanded {
                    expanded_paths.insert(entry.path.to_string());
                }
            }
            if !parent_path.is_empty() {
                expanded_paths.insert(parent_path);
            }

            let data_path = ui.get_install_data_path().to_string();
            let entries = build_install_tree_with_state(&staging_path, &expanded_paths, &data_path);
            ui.set_install_tree(ModelRc::new(VecModel::from(entries)));
            ui.set_show_install_tree_menu(false);
        });
    }

    ui.run().unwrap();
}

/// Build a LaunchConfig with VFS path translation.
///
/// If VFS is mounted, executable and working directory paths that point
/// inside the game directory or a mod's Root/ folder are translated to
/// their VFS_FUSE/ equivalents. External tools keep their original paths.
fn build_launch_config_vfs(exe: &Executable, state: &AppState) -> LaunchConfig {
    let instance = state.instance.as_ref();
    let exe_path = Path::new(&exe.binary);
    let game_root = instance.and_then(resolve_game_root_dir);

    // Determine the actual binary path — translate through VFS if mounted
    let binary = if let (Some(inst), Some(mm)) = (instance, state.mount_manager.as_ref()) {
        if mm.is_mounted() {
            let game_dir = game_root.clone().unwrap_or_default();
            let mount_point = mm.mount_point();
            let translated =
                vfs_launch::translate_exe_to_vfs(exe_path, &game_dir, mount_point, inst);
            if translated != exe_path {
                tracing::info!(
                    "VFS path translation: {} -> {}",
                    exe.binary,
                    translated.display()
                );
            }
            translated
        } else {
            exe_path.to_path_buf()
        }
    } else {
        exe_path.to_path_buf()
    };

    let args: Vec<String> = if exe.arguments.is_empty() {
        Vec::new()
    } else {
        exe.arguments.split_whitespace().map(String::from).collect()
    };

    let mut config = LaunchConfig::new(&binary).with_arguments(args);

    // Translate working directory through VFS if applicable
    if !exe.working_directory.is_empty() {
        let wd = if let (Some(_inst), Some(mm)) = (instance, state.mount_manager.as_ref()) {
            if mm.is_mounted() {
                let game_dir = game_root.clone().unwrap_or_default();
                vfs_launch::translate_working_dir_to_vfs(
                    Path::new(&exe.working_directory),
                    &game_dir,
                    mm.mount_point(),
                )
            } else {
                PathBuf::from(&exe.working_directory)
            }
        } else {
            PathBuf::from(&exe.working_directory)
        };
        config = config.with_working_dir(wd);
    } else if let (Some(inst), Some(mm)) = (instance, state.mount_manager.as_ref()) {
        // If no working dir is set but the exe is a VFS executable,
        // use the VFS mount point as the working directory
        if mm.is_mounted() {
            let game_dir = game_root.clone().unwrap_or_default();
            if vfs_launch::is_vfs_executable(exe_path, &game_dir, inst) {
                config = config.with_working_dir(mm.mount_point());
            }
        }
    }

    // Apply Proton settings: prefer Fluorine global prefix, fall back to instance config
    if let Some(fluorine) = mo2core::fluorine::FluorineConfig::load().filter(|c| c.prefix_exists())
    {
        config = config.with_proton(&fluorine.proton_path);
        config = config.with_prefix(&fluorine.prefix_path);
        // NOTE: Do NOT use fluorine.app_id for SteamAppId — that's the shortcut ID,
        // not the real game app ID. Steam DRM needs the real game's app ID.
    } else if let Some(instance) = instance {
        if let Some(proton_path) = instance.config.proton_path() {
            config = config.with_proton(proton_path);
        }
        if let Some(prefix_path) = instance.config.wine_prefix_path() {
            config = config.with_prefix(prefix_path);
        }
    }

    // Set the real game's Steam App ID for DRM authentication.
    // Priority: per-executable override > GameDef > instance config
    if !exe.steam_app_id.is_empty() {
        if let Ok(app_id) = exe.steam_app_id.parse::<u32>() {
            config = config.with_steam_app_id(app_id);
        }
    } else if let Some(inst) = instance {
        // Use the real game app ID from GameDef (e.g., 489830 for Skyrim SE)
        if let Some(gd) = GameDef::from_instance(inst.game_name(), game_root.as_deref()) {
            if let Some(app_id) = gd.steam_app_id {
                config = config.with_steam_app_id(app_id);
            }
        }
    }

    config
}

/// Load an instance and set up all state/UI.
fn load_instance(ui: &MainWindow, state: &mut AppState, path: &Path) -> Result<(), ()> {
    // Proactively clear any stale mount from previous crashes/runs so users
    // never need to run fusermount manually.
    let vfs_mount = path.join("VFS_FUSE");
    FuseController::try_cleanup_stale_mount_at(&vfs_mount);

    match Instance::load(path) {
        Ok(instance) => {
            // Load executables from INI (normalize imported Wine/Windows paths)
            let executables = ExecutablesList::read_from_ini_with_prefix(
                &instance.config.ini,
                instance.config.wine_prefix_path().as_deref(),
            );

            let path_str = path.display().to_string();
            ui.set_instance_path(SharedString::from(path_str.as_str()));
            ui.set_instance_display_path(SharedString::from(display_path(&path_str, 3).as_str()));

            // Set settings path properties from configured dirs (not hardcoded defaults)
            ui.set_settings_base_dir(SharedString::from(
                instance.mods_dir().display().to_string().as_str(),
            ));
            ui.set_settings_downloads_dir(instance.downloads_dir().display().to_string().into());
            ui.set_settings_mods_dir(instance.mods_dir().display().to_string().into());
            ui.set_settings_profiles_dir(instance.profiles_dir().display().to_string().into());
            ui.set_settings_overwrite_dir(instance.overwrite_dir().display().to_string().into());

            // Load Fluorine prefix status
            if let Some(fluorine) = mo2core::fluorine::FluorineConfig::load() {
                if fluorine.prefix_exists() {
                    ui.set_prefix_exists(true);
                    ui.set_prefix_status_text(SharedString::from(
                        format!(
                            "AppID: {} | Proton: {}",
                            fluorine.app_id, fluorine.proton_name
                        )
                        .as_str(),
                    ));
                    ui.set_wine_prefix_path(SharedString::from(fluorine.prefix_path.as_str()));
                    ui.set_steam_app_id(SharedString::from(fluorine.app_id.to_string().as_str()));
                    ui.set_selected_proton_name(SharedString::from(fluorine.proton_name.as_str()));
                    ui.set_selected_proton_path(SharedString::from(fluorine.proton_path.as_str()));
                } else {
                    ui.set_prefix_exists(false);
                    ui.set_prefix_status_text(SharedString::from(
                        "Prefix directory missing — recreate it",
                    ));
                }
            } else {
                ui.set_prefix_exists(false);
                ui.set_prefix_status_text(SharedString::from("No prefix configured"));
                ui.set_selected_proton_name(SharedString::default());
                ui.set_selected_proton_path(SharedString::default());
                ui.set_wine_prefix_path(SharedString::default());
                ui.set_steam_app_id(SharedString::default());
            }

            // Populate available Protons list
            let protons = nak_rust::steam::find_steam_protons();
            let proton_entries: Vec<ProtonEntry> = protons
                .iter()
                .map(|p| ProtonEntry {
                    name: SharedString::from(p.name.as_str()),
                    path: SharedString::from(p.path.display().to_string().as_str()),
                })
                .collect();
            let proton_names: Vec<SharedString> = protons
                .iter()
                .map(|p| SharedString::from(p.name.as_str()))
                .collect();
            ui.set_available_protons(ModelRc::new(VecModel::from(proton_entries)));
            ui.set_proton_names(ModelRc::new(VecModel::from(proton_names)));

            state.executables = executables;
            state.instance = Some(instance);

            // Compute conflict cache
            refresh_conflict_cache(state);

            // Auto-mount VFS
            auto_mount_vfs(state);
            ui.set_fuse_mounted(is_vfs_mounted(state));

            // Save as last instance
            state.global_settings.set_last_instance(&path_str);
            let _ = state.global_settings.save();

            refresh_ui(ui, state);
            refresh_tool_list(ui, state);
            Ok(())
        }
        Err(e) => {
            tracing::error!("Failed to load instance: {e}");
            Err(())
        }
    }
}

/// Deploy the managed plugins.txt to the Wine prefix so the game loads our plugins.
///
/// Bethesda games read their load order from AppData/Local/<game>/Plugins.txt.
/// We write this file before each launch to ensure the game loads the right plugins.
fn deploy_plugins_to_prefix(state: &AppState) {
    let Some(ref instance) = state.instance else {
        return;
    };

    // Need GameDef for the AppData folder name
    let Some(game_def) = GameDef::from_instance(
        instance.game_name(),
        instance.config.game_directory().as_deref(),
    ) else {
        tracing::warn!("Cannot deploy plugins: no GameDef for this game");
        return;
    };

    // The my_games_folder is also the AppData/Local folder name
    let Some(ref appdata_folder) = game_def.my_games_folder else {
        tracing::warn!("Cannot deploy plugins: no AppData folder name for this game");
        return;
    };

    // Get the Wine prefix path
    let prefix_path = if let Some(fluorine) =
        mo2core::fluorine::FluorineConfig::load().filter(|c| c.prefix_exists())
    {
        PathBuf::from(&fluorine.prefix_path)
    } else if let Some(prefix) = instance.config.wine_prefix_path() {
        prefix.to_path_buf()
    } else {
        tracing::warn!("Cannot deploy plugins: no Wine prefix configured");
        return;
    };

    let Ok(prefix) = mo2core::launcher::wine_prefix::WinePrefix::load(&prefix_path) else {
        tracing::warn!(
            "Cannot deploy plugins: invalid Wine prefix at {:?}",
            prefix_path
        );
        return;
    };

    // Prefer profile-authored plugins/loadorder files (MO2-compatible behavior).
    // Fallback to generated list if profile files are unavailable.
    let mut deployed_from_profile = false;
    if let Some(ref profile) = instance.active_profile {
        match mo2core::launcher::wine_prefix::deploy_profile_plugin_files(
            &prefix,
            appdata_folder,
            &profile.path,
        ) {
            Ok(()) => {
                deployed_from_profile = true;
            }
            Err(e) => {
                tracing::warn!(
                    "Failed to deploy profile plugin files (falling back to generated list): {e}"
                );
            }
        }
    }

    if !deployed_from_profile {
        // Build the plugin list and collect (filename, enabled) pairs
        let plist = instance.build_plugin_list();
        let mut sorted_plugins = plist.plugins.clone();
        sorted_plugins.sort_by_key(|p| p.priority);
        let plugins: Vec<(String, bool)> = sorted_plugins
            .iter()
            .map(|p| (p.filename.clone(), p.enabled))
            .collect();

        tracing::info!(
            "Deploying generated plugins.txt ({} plugins) to prefix {:?} / {}",
            plugins.len(),
            prefix_path,
            appdata_folder
        );
        for (name, enabled) in &plugins {
            tracing::info!("  plugin: '{}' (enabled={})", name, enabled);
        }

        if let Err(e) =
            mo2core::launcher::wine_prefix::deploy_plugins(&prefix, appdata_folder, &plugins)
        {
            tracing::error!("Failed to deploy plugins.txt: {e}");
        }
    }

    // Proactively enforce a safer launch display mode for Skyrim titles.
    if matches!(
        game_def.id,
        GameId::SkyrimSE | GameId::SkyrimLE | GameId::SkyrimVR
    ) {
        if let Err(e) =
            mo2core::launcher::wine_prefix::enforce_skyrim_window_mode(&prefix, appdata_folder)
        {
            tracing::warn!("Failed to enforce Skyrim display mode: {e}");
        }
    }

    // Deploy profile-local INI files if LocalSettings=true
    if let Some(ref profile) = instance.active_profile {
        if let Some(ref my_games) = game_def.my_games_folder {
            if profile.local_settings() {
                let ini_files: Vec<String> =
                    game_def.ini_files.iter().map(|s| s.to_string()).collect();
                if let Err(e) = mo2core::launcher::wine_prefix::deploy_profile_ini(
                    &prefix,
                    my_games,
                    &profile.path,
                    &ini_files,
                ) {
                    tracing::error!("Failed to deploy profile INI files: {e}");
                }
            }

            // Deploy profile-local saves if LocalSaves=true
            if profile.local_saves() {
                let save_subdir = mo2core::launcher::wine_prefix::resolve_profile_save_subdir(
                    &profile.path,
                    &game_def.ini_files,
                );
                if let Err(e) = mo2core::launcher::wine_prefix::deploy_profile_saves(
                    &prefix,
                    my_games,
                    &profile.path,
                    &save_subdir,
                ) {
                    tracing::error!("Failed to deploy profile saves: {e}");
                }
            }
        }
    }
}

/// Check if the full-game VFS is currently mounted.
fn is_vfs_mounted(state: &AppState) -> bool {
    state
        .mount_manager
        .as_ref()
        .is_some_and(|mm| mm.is_mounted())
}

/// Check if a path is currently mounted according to /proc/mounts.
fn is_kernel_mountpoint(path: &Path) -> bool {
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

/// Resolve the game's data directory name (e.g., "Data", "Data Files").
/// Falls back to "Data" if GameDef can't be resolved for the instance.
fn resolve_data_dir_name(instance: &Instance) -> String {
    GameDef::from_instance(
        instance.game_name(),
        instance.config.game_directory().as_deref(),
    )
    .map(|gd| gd.data_dir_name.to_string())
    .unwrap_or_else(|| "Data".to_string())
}

/// Resolve the effective game root directory for VFS/launch logic.
///
/// If the configured game path points directly at the game's data directory
/// (e.g. `<game>/Data`), use its parent as the game root.
fn resolve_game_root_dir(instance: &Instance) -> Option<PathBuf> {
    let configured = instance.config.game_directory()?;
    let data_dir_name = resolve_data_dir_name(instance);
    let configured_name = configured.file_name().and_then(|n| n.to_str());

    if configured_name.is_some_and(|n| n.eq_ignore_ascii_case(&data_dir_name)) {
        if let Some(parent) = configured.parent() {
            return Some(parent.to_path_buf());
        }
    }

    Some(configured)
}

/// Auto-mount the full-game VFS at <instance>/VFS_FUSE/.
///
/// Merges the entire game directory with mods into a single view.
/// If already mounted, this is a no-op. If the mount_manager exists
/// but isn't mounted, rebuilds and remounts.
fn auto_mount_vfs(state: &mut AppState) {
    let Some(ref instance) = state.instance else {
        return;
    };

    let mount_point = instance.root.join("VFS_FUSE");
    let kernel_mounted = is_kernel_mountpoint(&mount_point);

    // Already mounted and healthy in both userland and kernel view.
    if is_vfs_mounted(state) && kernel_mounted {
        return;
    }

    // If kernel still has a mount but our session tracking disagrees,
    // clean it up before remounting to avoid stale/half-dead FUSE sessions.
    if kernel_mounted && !is_vfs_mounted(state) {
        tracing::warn!(
            "Detected stale kernel mount at {:?} without active session; cleaning up",
            mount_point
        );
        FuseController::try_cleanup_stale_mount_at(&mount_point);
        if is_kernel_mountpoint(&mount_point) {
            tracing::error!(
                "Kernel mount at {:?} is still present after cleanup; refusing remount",
                mount_point
            );
            return;
        }
        state.mount_manager = None;
    }

    // If session exists but kernel mount is gone, drop stale manager and recreate.
    if !kernel_mounted && is_vfs_mounted(state) {
        tracing::warn!(
            "Detected stale mount-manager state for {:?}; recreating mount",
            mount_point
        );
        state.mount_manager = None;
    }

    let Some(game_dir) = resolve_game_root_dir(instance) else {
        tracing::warn!("No game directory configured — cannot mount VFS");
        return;
    };

    let overwrite_dir = instance.overwrite_dir();
    let data_dir_name = resolve_data_dir_name(instance);

    let mut mm = MountManager::new(&mount_point, &overwrite_dir);

    let active = instance.active_mods_sorted();
    let mods: Vec<(&str, &Path)> = active.iter().map(|(n, p)| (*n, *p)).collect();

    match mm.mount(&game_dir, &data_dir_name, &mods) {
        Ok(()) => {
            tracing::info!(
                "Full-game VFS mounted at {:?} (game: {:?}, {} mods)",
                mount_point,
                game_dir,
                mods.len()
            );
            state.mount_manager = Some(mm);
        }
        Err(e) => tracing::error!("Failed to mount full-game VFS: {e}"),
    }
}

/// Rebuild VFS tree if currently mounted (after mod toggle / profile switch).
fn rebuild_vfs_if_mounted(state: &mut AppState) {
    let Some(ref instance) = state.instance else {
        return;
    };
    let Some(ref mm) = state.mount_manager else {
        return;
    };
    if !mm.is_mounted() {
        return;
    }

    let Some(game_dir) = resolve_game_root_dir(instance) else {
        return;
    };

    let data_dir_name = resolve_data_dir_name(instance);
    let active = instance.active_mods_sorted();
    let mods: Vec<(&str, &Path)> = active.iter().map(|(n, p)| (*n, *p)).collect();

    if let Err(e) = mm.rebuild(&game_dir, &data_dir_name, &mods) {
        tracing::error!("Failed to rebuild VFS: {e}");
    } else {
        tracing::info!("Full-game VFS rebuilt with {} mods", mods.len());
    }
}

/// Refresh the instance list on page 0.
fn refresh_instance_list(ui: &MainWindow, global_settings: &GlobalSettings) {
    let mut entries = Vec::new();
    let mut seen_paths = HashSet::new();
    let global_root = GlobalSettings::global_instances_root();

    // Global instances
    match list_global_instances() {
        Ok(instances) => {
            for info in instances {
                let full_path = info.path.display().to_string();
                seen_paths.insert(full_path.clone());
                entries.push(InstanceEntry {
                    name: SharedString::from(info.name.as_str()),
                    game_name: SharedString::from(info.game_name.as_str()),
                    display_path: SharedString::from(display_path(&full_path, 3).as_str()),
                    path: SharedString::from(full_path.as_str()),
                    is_portable: info.is_portable,
                });
            }
        }
        Err(e) => tracing::warn!("Failed to list global instances: {e}"),
    }

    // Also include recently-used external instances.
    let mut recent = global_settings.recent_instances();
    if let Some(last) = global_settings.last_instance() {
        if !recent.iter().any(|p| p == last) {
            recent.insert(0, last.to_string());
        }
    }

    for path_str in recent {
        if seen_paths.contains(&path_str) {
            continue;
        }
        let path = PathBuf::from(&path_str);
        if !path.join("ModOrganizer.ini").exists() {
            continue;
        }

        match portable_instance_info(&path) {
            Ok(mut info) => {
                if !info.path.starts_with(&global_root) {
                    info.is_portable = true;
                }
                entries.push(InstanceEntry {
                    name: SharedString::from(info.name.as_str()),
                    game_name: SharedString::from(info.game_name.as_str()),
                    display_path: SharedString::from(display_path(&path_str, 3).as_str()),
                    path: SharedString::from(path_str.as_str()),
                    is_portable: info.is_portable,
                });
                seen_paths.insert(path_str);
            }
            Err(e) => tracing::warn!("Failed to inspect recent instance {:?}: {e}", path),
        }
    }

    ui.set_instance_list(ModelRc::new(VecModel::from(entries)));
}

/// Compute and cache conflict data for the current instance.
fn refresh_conflict_cache(state: &mut AppState) {
    let Some(ref instance) = state.instance else {
        state.conflict_cache.clear();
        state.vfs_tree = None;
        return;
    };

    let active_mods: Vec<(String, PathBuf)> = instance
        .active_mods_sorted()
        .into_iter()
        .map(|(name, path)| (name.to_string(), path.to_path_buf()))
        .collect();

    state.conflict_cache = conflict::detect_conflicts(&active_mods);

    // Build full-game VFS tree for data tab (game dir + mods + overwrite)
    let overwrite_dir = instance.overwrite_dir();
    let data_dir_name = resolve_data_dir_name(instance);
    let game_dir = resolve_game_root_dir(instance);
    let mod_refs: Vec<(&str, &Path)> = active_mods
        .iter()
        .map(|(n, p)| (n.as_str(), p.as_path()))
        .collect();
    match game_dir {
        Some(ref gd) => match build_full_game_vfs(gd, &data_dir_name, &mod_refs, &overwrite_dir) {
            Ok(tree) => {
                state.vfs_tree = Some(tree);
            }
            Err(e) => {
                tracing::warn!("Failed to build VFS tree: {e}");
                state.vfs_tree = None;
            }
        },
        None => {
            state.vfs_tree = None;
        }
    }
}

/// Sync all UI properties from the current instance state.
fn refresh_ui(ui: &MainWindow, state: &AppState) {
    let Some(ref instance) = state.instance else {
        ui.set_game_name(SharedString::default());
        ui.set_game_path(SharedString::default());
        ui.set_active_profile(SharedString::default());
        ui.set_mod_count(0);
        ui.set_enabled_mod_count(0);
        ui.set_plugin_count(0);
        ui.set_enabled_plugin_count(0);
        ui.set_profile_list(ModelRc::default());
        ui.set_mod_list(ModelRc::default());
        ui.set_selected_mod_name(SharedString::default());
        ui.set_selected_mod_winning(SharedString::default());
        ui.set_selected_mod_losing(SharedString::default());
        ui.set_plugin_list(ModelRc::default());
        return;
    };

    ui.set_game_name(instance.game_name().unwrap_or("Unknown").into());

    // Game path
    if let Some(game_dir) = instance.config.game_directory() {
        ui.set_game_path(game_dir.display().to_string().into());
    } else {
        ui.set_game_path(SharedString::default());
    }

    if let Some(ref profile) = instance.active_profile {
        ui.set_active_profile(SharedString::from(profile.name.as_str()));
    }

    let mod_count = instance
        .mods
        .iter()
        .filter(|m| !m.is_overwrite() && !m.is_separator())
        .count() as i32;
    let enabled_count = instance
        .mods
        .iter()
        .filter(|m| m.enabled && !m.is_overwrite() && !m.is_separator())
        .count() as i32;
    ui.set_mod_count(mod_count);
    ui.set_enabled_mod_count(enabled_count);

    // Profile list
    let profiles = instance.list_profiles().unwrap_or_default();
    let profile_model: Vec<SharedString> = profiles
        .iter()
        .map(|p| SharedString::from(p.as_str()))
        .collect();
    ui.set_profile_list(ModelRc::new(VecModel::from(profile_model)));

    // Mod list (with conflict data, excluding Overwrite — shown separately)
    let mut mod_entries = build_mod_list(instance, &state.conflict_cache);
    // Remove overwrite from the model — it's shown as a separate UI element
    mod_entries.retain(|e| !e.is_overwrite);
    let column = ui.get_sort_column();
    let ascending = ui.get_sort_ascending();
    sort_mod_entries(&mut mod_entries, column, ascending);
    apply_collapse_state(&mut mod_entries, &state.collapsed_separators);
    apply_selection_state_to_mod_entries(ui, &mut mod_entries);
    tracing::info!("Mod list: {} entries in model", mod_entries.len(),);
    for entry in &mod_entries {
        tracing::info!(
            "  mod: '{}' (enabled={}, priority={})",
            entry.name,
            entry.enabled,
            entry.priority
        );
    }
    ui.set_mod_list(ModelRc::new(VecModel::from(mod_entries)));
    let selected_name = ui.get_selected_mod_name().to_string();
    if !selected_name.is_empty() {
        let has_selected = instance
            .mods
            .iter()
            .any(|m| !m.is_overwrite() && m.name == selected_name);
        if !has_selected {
            ui.set_selected_mod_name(SharedString::default());
        }
    }

    // Filter items
    let filter_items = build_filter_items(instance);
    ui.set_filter_items(ModelRc::new(VecModel::from(filter_items)));

    // Plugin list
    let plist = instance.build_plugin_list();
    ui.set_plugin_count(plist.count() as i32);
    ui.set_enabled_plugin_count(plist.enabled_count() as i32);
    let mut plugin_entries = build_plugin_list_entries(&plist);
    let plugin_column = ui.get_plugin_sort_column();
    let plugin_ascending = ui.get_plugin_sort_ascending();
    sort_plugin_entries(&mut plugin_entries, plugin_column, plugin_ascending);
    ui.set_plugin_list(ModelRc::new(VecModel::from(plugin_entries)));

    // Archives tab
    let archives = instance.scan_archives();
    ui.set_archive_count(archives.len() as i32);
    let archive_entries: Vec<ArchiveEntry> = archives
        .iter()
        .map(|(filename, origin, enabled)| ArchiveEntry {
            filename: SharedString::from(filename.as_str()),
            origin_mod: SharedString::from(origin.as_str()),
            mod_enabled: *enabled,
        })
        .collect();
    ui.set_archive_list(ModelRc::new(VecModel::from(archive_entries)));

    // Data tab (VFS tree)
    let data_entries = build_data_entries(instance, &state.vfs_tree, &state.data_expanded);
    ui.set_data_file_count(
        state
            .vfs_tree
            .as_ref()
            .map(|t| t.file_count as i32)
            .unwrap_or(0),
    );
    ui.set_data_list(ModelRc::new(VecModel::from(data_entries)));

    // Downloads tab
    let downloads_dir = instance.downloads_dir();
    let download_infos = download::scan_downloads(&downloads_dir).unwrap_or_default();
    let installed_files: HashSet<String> = instance
        .mods
        .iter()
        .filter_map(|m| m.installation_file().map(|f| f.to_lowercase()))
        .collect();
    ui.set_download_count(download_infos.len() as i32);
    let download_entries: Vec<DownloadEntry> = download_infos
        .iter()
        .map(|d| {
            let installed = installed_files.contains(&d.filename.to_lowercase());
            DownloadEntry {
                filename: SharedString::from(d.filename.as_str()),
                size_text: SharedString::from(download::format_size(d.size).as_str()),
                nexus_id: d.nexus_id.unwrap_or(0) as i32,
                version: SharedString::from(d.version.as_deref().unwrap_or("")),
                installed,
            }
        })
        .collect();
    ui.set_download_list(ModelRc::new(VecModel::from(download_entries)));

    // Saves tab
    let save_entries = build_save_entries(instance);
    ui.set_save_count(save_entries.len() as i32);
    ui.set_save_list(ModelRc::new(VecModel::from(save_entries)));
}

/// Refresh the tool list from executables state.
fn refresh_tool_list(ui: &MainWindow, state: &AppState) {
    let tool_entries: Vec<ExecutableEntry> = state
        .executables
        .executables
        .iter()
        .map(|exe| ExecutableEntry {
            title: SharedString::from(exe.title.as_str()),
            binary: SharedString::from(exe.binary.as_str()),
            arguments: SharedString::from(exe.arguments.as_str()),
            working_directory: SharedString::from(exe.working_directory.as_str()),
            show_in_toolbar: exe.show_in_toolbar,
            hide: exe.hide,
        })
        .collect();
    ui.set_tool_list(ModelRc::new(VecModel::from(tool_entries)));
    refresh_toolbar_tools(ui, state);
}

/// Populate the executable ComboBox with all non-hidden executables.
fn refresh_toolbar_tools(ui: &MainWindow, state: &AppState) {
    let toolbar_titles: Vec<SharedString> = state
        .executables
        .executables
        .iter()
        .filter(|exe| !exe.hide)
        .map(|exe| SharedString::from(exe.title.as_str()))
        .collect();

    // If the currently selected tool is no longer in the list, pick the first one
    let current = ui.get_selected_toolbar_tool().to_string();
    let still_valid = toolbar_titles.iter().any(|t| t.as_str() == current);
    if !still_valid {
        if let Some(first) = toolbar_titles.first() {
            ui.set_selected_toolbar_tool(first.clone());
        } else {
            ui.set_selected_toolbar_tool(SharedString::default());
        }
    }

    ui.set_has_toolbar_tools(!toolbar_titles.is_empty());
    ui.set_toolbar_tools(ModelRc::new(VecModel::from(toolbar_titles)));
}

/// Truncate a path to show the last N components with a leading "…/" prefix.
fn display_path(path: &str, max_components: usize) -> String {
    let parts: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
    if parts.len() <= max_components {
        path.to_string()
    } else {
        format!("…/{}", parts[parts.len() - max_components..].join("/"))
    }
}

/// Persist the active profile's modlist.txt to disk.
fn save_active_modlist(instance: &Instance) {
    if let Some(ref profile) = instance.active_profile {
        let modlist_path = profile.path.join("modlist.txt");
        if let Err(e) = profile.modlist.write(&modlist_path) {
            tracing::error!("Failed to save modlist.txt: {e}");
        }
    }
}

/// Convert PluginList to Slint PluginEntry structs.
fn build_plugin_list_entries(plugin_list: &PluginList) -> Vec<PluginEntry> {
    plugin_list
        .plugins
        .iter()
        .map(|p| {
            let type_str = if p.is_light {
                "ESL"
            } else if p.is_master {
                "ESM"
            } else {
                "ESP"
            };
            PluginEntry {
                filename: SharedString::from(p.filename.as_str()),
                enabled: p.enabled,
                priority: p.priority,
                plugin_type: SharedString::from(type_str),
                origin_mod: SharedString::from(p.origin_mod.as_deref().unwrap_or("")),
                is_master: p.is_master,
                is_light: p.is_light,
                is_locked: p.is_locked,
            }
        })
        .collect()
}

/// Build filter items from categories and content types.
fn build_filter_items(instance: &Instance) -> Vec<FilterItem> {
    let mut items = Vec::new();

    // --- Categories section ---
    items.push(FilterItem {
        name: SharedString::from("Categories"),
        depth: 0,
        checked: false,
        is_expanded: true,
        has_children: true,
        count: 0,
    });

    // Add top-level categories, then children
    let top_cats = instance.categories.top_level();
    for cat in &top_cats {
        let child_cats = instance.categories.children(cat.id);
        let mod_count = instance
            .mods
            .iter()
            .filter(|m| {
                m.meta
                    .as_ref()
                    .map(|meta| meta.category_ids().contains(&cat.id))
                    .unwrap_or(false)
            })
            .count();

        items.push(FilterItem {
            name: SharedString::from(cat.name.as_str()),
            depth: 1,
            checked: false,
            is_expanded: false,
            has_children: !child_cats.is_empty(),
            count: mod_count as i32,
        });

        for child in &child_cats {
            let child_count = instance
                .mods
                .iter()
                .filter(|m| {
                    m.meta
                        .as_ref()
                        .map(|meta| meta.category_ids().contains(&child.id))
                        .unwrap_or(false)
                })
                .count();

            items.push(FilterItem {
                name: SharedString::from(child.name.as_str()),
                depth: 2,
                checked: false,
                is_expanded: false,
                has_children: false,
                count: child_count as i32,
            });
        }
    }

    // --- Content Types section ---
    items.push(FilterItem {
        name: SharedString::from("Content"),
        depth: 0,
        checked: false,
        is_expanded: true,
        has_children: true,
        count: 0,
    });

    let content_labels = [
        ("Plugins", mo2core::modinfo::ContentType::Plugin),
        ("Textures", mo2core::modinfo::ContentType::Texture),
        ("Meshes", mo2core::modinfo::ContentType::Mesh),
        ("BSA/BA2", mo2core::modinfo::ContentType::BsaArchive),
        ("Scripts", mo2core::modinfo::ContentType::Script),
        ("Interface", mo2core::modinfo::ContentType::Interface),
        ("Sound", mo2core::modinfo::ContentType::Sound),
        ("Music", mo2core::modinfo::ContentType::Music),
        ("SKSE", mo2core::modinfo::ContentType::Skse),
        ("INI Files", mo2core::modinfo::ContentType::Ini),
    ];

    for (label, ct) in &content_labels {
        let count = instance
            .mods
            .iter()
            .filter(|m| m.content_types.contains(ct))
            .count();
        if count > 0 {
            items.push(FilterItem {
                name: SharedString::from(*label),
                depth: 1,
                checked: false,
                is_expanded: false,
                has_children: false,
                count: count as i32,
            });
        }
    }

    // --- Mod State section ---
    items.push(FilterItem {
        name: SharedString::from("State"),
        depth: 0,
        checked: false,
        is_expanded: true,
        has_children: true,
        count: 0,
    });

    let enabled_count = instance
        .mods
        .iter()
        .filter(|m| m.enabled && !m.is_overwrite() && !m.is_separator())
        .count();
    let disabled_count = instance
        .mods
        .iter()
        .filter(|m| !m.enabled && !m.is_overwrite() && !m.is_separator())
        .count();
    let conflict_count = instance
        .mods
        .iter()
        .filter(|m| !m.flags.is_empty() || !m.content_types.is_empty())
        .count();

    items.push(FilterItem {
        name: SharedString::from("Enabled"),
        depth: 1,
        checked: false,
        is_expanded: false,
        has_children: false,
        count: enabled_count as i32,
    });
    items.push(FilterItem {
        name: SharedString::from("Disabled"),
        depth: 1,
        checked: false,
        is_expanded: false,
        has_children: false,
        count: disabled_count as i32,
    });

    let _ = conflict_count; // Available for future use

    items
}

/// Apply category/filter panel selections to mod entries.
fn apply_category_filter(
    entries: &mut Vec<ModEntry>,
    filter_items: &ModelRc<FilterItem>,
    _and_mode: bool,
) {
    // Collect checked filter names
    let mut checked_names: Vec<String> = Vec::new();
    for i in 0..filter_items.row_count() {
        let item = filter_items.row_data(i).unwrap();
        if item.checked && item.depth > 0 {
            checked_names.push(item.name.to_string());
        }
    }

    if checked_names.is_empty() {
        return; // No filters active, show all
    }

    entries.retain(|entry| {
        // Always keep separators and overwrite
        if entry.is_separator || entry.is_overwrite {
            return true;
        }

        // Check if mod matches any checked filter
        for name in &checked_names {
            // Category match
            if entry.category_name.as_str() == name.as_str() {
                return true;
            }
            // State match
            if name == "Enabled" && entry.enabled {
                return true;
            }
            if name == "Disabled" && !entry.enabled {
                return true;
            }
            // Content match (check if content-text contains the first letter abbreviation)
            let content_match = match name.as_str() {
                "Plugins" => entry.content_text.contains('P'),
                "Textures" => entry.content_text.contains('T'),
                "Meshes" => entry.content_text.contains('M'),
                "BSA/BA2" => entry.content_text.contains('B'),
                "Scripts" => entry.content_text.contains('S'),
                "Interface" => entry.content_text.contains('I'),
                "Sound" => entry.content_text.contains('A'),
                "Music" => entry.content_text.contains('U'),
                "SKSE" => entry.content_text.contains('K'),
                "INI Files" => entry.content_text.contains('C'),
                _ => false,
            };
            if content_match {
                return true;
            }
        }

        false
    });
}

/// Sort mod entries by column index.
fn sort_mod_entries(entries: &mut [ModEntry], column: i32, ascending: bool) {
    match column {
        0 => entries.sort_by(|a, b| {
            let cmp = a
                .display_name
                .to_lowercase()
                .cmp(&b.display_name.to_lowercase());
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        1 => entries.sort_by(|a, b| {
            let cmp = a.priority.cmp(&b.priority);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        2 => entries.sort_by(|a, b| {
            let cmp = a.mod_type.cmp(&b.mod_type);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        3 => entries.sort_by(|a, b| {
            let cmp = a.version.cmp(&b.version);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        4 => entries.sort_by(|a, b| {
            let cmp = a.flags_text.cmp(&b.flags_text);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        5 => entries.sort_by(|a, b| {
            let cmp = a
                .category_name
                .to_lowercase()
                .cmp(&b.category_name.to_lowercase());
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        6 => entries.sort_by(|a, b| {
            let cmp = a.content_text.cmp(&b.content_text);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        7 => entries.sort_by(|a, b| {
            let cmp = a.conflict_text.cmp(&b.conflict_text);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        8 => entries.sort_by(|a, b| {
            let cmp = a.nexus_id.cmp(&b.nexus_id);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        9 => entries.sort_by(|a, b| {
            let cmp = a.notes.to_lowercase().cmp(&b.notes.to_lowercase());
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        _ => {}
    }
}

/// Sort plugin entries by column index.
fn sort_plugin_entries(entries: &mut [PluginEntry], column: i32, ascending: bool) {
    match column {
        0 => entries.sort_by(|a, b| {
            let cmp = a.filename.to_lowercase().cmp(&b.filename.to_lowercase());
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        1 => entries.sort_by(|a, b| {
            let cmp = a.priority.cmp(&b.priority);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        2 => entries.sort_by(|a, b| {
            let cmp = a.plugin_type.cmp(&b.plugin_type);
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        3 => entries.sort_by(|a, b| {
            let cmp = a
                .origin_mod
                .to_lowercase()
                .cmp(&b.origin_mod.to_lowercase());
            if ascending {
                cmp
            } else {
                cmp.reverse()
            }
        }),
        _ => {}
    }
}

/// Update per-row conflict highlight flags based on the currently selected mod.
/// Returns the selected mod's conflict summary strings if the selected mod is in the list.
fn apply_selected_conflict_highlight(
    entries: &mut [ModEntry],
    selected_name: &str,
) -> Option<(SharedString, SharedString)> {
    for e in entries.iter_mut() {
        e.highlight_winning = false;
        e.highlight_losing = false;
    }

    if selected_name.is_empty() {
        return None;
    }

    let selected = entries.iter().find(|e| e.name.as_str() == selected_name)?;
    let win_keys = selected.winning_opponent_keys.to_string();
    let lose_keys = selected.losing_opponent_keys.to_string();
    let win_text = selected.winning_opponents.clone();
    let lose_text = selected.losing_opponents.clone();

    for e in entries.iter_mut() {
        if e.name.as_str() == selected_name || e.is_separator || e.is_overwrite {
            continue;
        }
        let token = format!("|{}|", e.name);
        let is_losing_peer = lose_keys.contains(&token);
        let is_winning_peer = win_keys.contains(&token);
        e.highlight_losing = is_losing_peer;
        e.highlight_winning = !is_losing_peer && is_winning_peer;
    }

    Some((win_text, lose_text))
}

fn apply_selection_state_to_mod_entries(ui: &MainWindow, entries: &mut [ModEntry]) {
    let selected_name = ui.get_selected_mod_name().to_string();
    if let Some((winning, losing)) = apply_selected_conflict_highlight(entries, &selected_name) {
        ui.set_selected_mod_winning(winning);
        ui.set_selected_mod_losing(losing);
    } else {
        if !selected_name.is_empty() {
            ui.set_selected_mod_name(SharedString::default());
        }
        ui.set_selected_mod_winning(SharedString::default());
        ui.set_selected_mod_losing(SharedString::default());
    }
}

/// Convert Instance mods to Slint ModEntry structs.
fn build_mod_list(instance: &Instance, conflicts: &HashMap<String, ModConflicts>) -> Vec<ModEntry> {
    fn build_opponent_keys(names: &[&str]) -> String {
        if names.is_empty() {
            String::new()
        } else {
            let mut out = String::new();
            for name in names {
                out.push('|');
                out.push_str(name);
            }
            out.push('|');
            out
        }
    }

    instance
        .mods
        .iter()
        .map(|m| {
            let type_str = match m.mod_type {
                mo2core::modinfo::ModType::Regular => "Regular",
                mo2core::modinfo::ModType::Separator => "Separator",
                mo2core::modinfo::ModType::Overwrite => "Overwrite",
                mo2core::modinfo::ModType::Foreign => "Foreign",
                mo2core::modinfo::ModType::Backup => "Backup",
            };

            // Conflict info
            let mod_conflicts = conflicts.get(&m.name);
            let has_winning = mod_conflicts.map(|c| c.has_winning()).unwrap_or(false);
            let has_losing = mod_conflicts.map(|c| c.has_losing()).unwrap_or(false);

            // Flags text: compact indicators
            let mut flags = Vec::new();
            if has_winning && has_losing {
                flags.push("WL");
            } else if has_winning {
                flags.push("W");
            } else if has_losing {
                flags.push("L");
            }
            if m.has_update() {
                flags.push("!");
            }
            if m.notes().is_some_and(|n| !n.is_empty()) {
                flags.push("N");
            }
            let flags_text = flags.join(" ");

            // Conflict text: "W:3 L:5" format
            let conflict_text = match mod_conflicts {
                Some(c) if !c.is_empty() => {
                    let mut parts = Vec::new();
                    if !c.winning.is_empty() {
                        parts.push(format!("W:{}", c.winning.len()));
                    }
                    if !c.losing.is_empty() {
                        parts.push(format!("L:{}", c.losing.len()));
                    }
                    parts.join(" ")
                }
                _ => String::new(),
            };

            // Opponent mod names (unique, sorted)
            let winning_opponents = match mod_conflicts {
                Some(c) if !c.winning.is_empty() => {
                    let mut names: Vec<&str> = c.winning.iter().map(|f| f.loser.as_str()).collect();
                    names.sort_unstable();
                    names.dedup();
                    names.join(", ")
                }
                _ => String::new(),
            };
            let winning_opponent_keys = match mod_conflicts {
                Some(c) if !c.winning.is_empty() => {
                    let mut names: Vec<&str> = c.winning.iter().map(|f| f.loser.as_str()).collect();
                    names.sort_unstable();
                    names.dedup();
                    build_opponent_keys(&names)
                }
                _ => String::new(),
            };
            let losing_opponents = match mod_conflicts {
                Some(c) if !c.losing.is_empty() => {
                    let mut names: Vec<&str> = c.losing.iter().map(|f| f.winner.as_str()).collect();
                    names.sort_unstable();
                    names.dedup();
                    names.join(", ")
                }
                _ => String::new(),
            };
            let losing_opponent_keys = match mod_conflicts {
                Some(c) if !c.losing.is_empty() => {
                    let mut names: Vec<&str> = c.losing.iter().map(|f| f.winner.as_str()).collect();
                    names.sort_unstable();
                    names.dedup();
                    build_opponent_keys(&names)
                }
                _ => String::new(),
            };

            // Content text: abbreviated content types
            let has_root = m.path.join("Root").is_dir();
            let mut content_parts: Vec<&str> = m
                .content_types
                .iter()
                .map(|ct| match ct {
                    mo2core::modinfo::ContentType::Plugin => "P",
                    mo2core::modinfo::ContentType::Texture => "T",
                    mo2core::modinfo::ContentType::Mesh => "M",
                    mo2core::modinfo::ContentType::BsaArchive => "B",
                    mo2core::modinfo::ContentType::Script => "S",
                    mo2core::modinfo::ContentType::Interface => "I",
                    mo2core::modinfo::ContentType::Sound => "A",
                    mo2core::modinfo::ContentType::Music => "U",
                    mo2core::modinfo::ContentType::Skse => "K",
                    mo2core::modinfo::ContentType::Ini => "C",
                    _ => "",
                })
                .collect();
            if has_root {
                content_parts.push("R");
            }
            let content_text = content_parts.join(" ");

            // Category name from meta.ini category IDs
            let category_name = m
                .meta
                .as_ref()
                .and_then(|meta| {
                    let ids = meta.category_ids();
                    ids.first().and_then(|&id| instance.categories.name(id))
                })
                .unwrap_or("");

            // Nexus ID
            let nexus_id = m.nexus_id().unwrap_or(0) as i32;

            // Notes
            let notes = m.notes().unwrap_or("");

            ModEntry {
                name: SharedString::from(m.name.as_str()),
                display_name: SharedString::from(m.display_name()),
                enabled: m.enabled,
                priority: m.priority,
                mod_type: SharedString::from(type_str),
                version: SharedString::from(m.version().unwrap_or("")),
                is_separator: m.is_separator(),
                is_overwrite: m.is_overwrite(),
                is_collapsed: false,
                separator_has_children: false,
                hidden_by_collapse: false,
                has_root,
                flags_text: SharedString::from(flags_text.as_str()),
                category_name: SharedString::from(category_name),
                content_text: SharedString::from(content_text.as_str()),
                conflict_text: SharedString::from(conflict_text.as_str()),
                has_winning,
                has_losing,
                winning_opponents: SharedString::from(winning_opponents.as_str()),
                winning_opponent_keys: SharedString::from(winning_opponent_keys.as_str()),
                losing_opponents: SharedString::from(losing_opponents.as_str()),
                losing_opponent_keys: SharedString::from(losing_opponent_keys.as_str()),
                highlight_winning: false,
                highlight_losing: false,
                nexus_id,
                notes: SharedString::from(notes),
            }
        })
        .collect()
}

/// Check if a process is still running by inspecting /proc/<pid>.
fn is_process_alive(pid: u32) -> bool {
    Path::new(&format!("/proc/{pid}")).exists()
}

/// Info needed to sync INI files back to the profile after a launched process exits.
#[derive(Clone)]
struct IniSyncInfo {
    prefix_path: PathBuf,
    my_games_folder: String,
    profile_path: PathBuf,
    ini_files: Vec<String>,
}

#[derive(Clone)]
struct SaveSyncInfo {
    prefix_path: PathBuf,
    my_games_folder: String,
    profile_path: PathBuf,
    save_subdir: String,
}

/// Build INI sync-back info from the current app state, if LocalSettings is enabled.
fn build_ini_sync_info(state: &AppState) -> Option<IniSyncInfo> {
    let instance = state.instance.as_ref()?;
    let profile = instance.active_profile.as_ref()?;
    if !profile.local_settings() {
        return None;
    }

    let game_def = GameDef::from_instance(
        instance.game_name(),
        instance.config.game_directory().as_deref(),
    )?;
    let my_games_folder = game_def.my_games_folder.clone()?;

    // Resolve prefix path (same logic as deploy_plugins_to_prefix)
    let prefix_path = if let Some(fluorine) =
        mo2core::fluorine::FluorineConfig::load().filter(|c| c.prefix_exists())
    {
        PathBuf::from(&fluorine.prefix_path)
    } else {
        instance.config.wine_prefix_path()?.to_path_buf()
    };

    Some(IniSyncInfo {
        prefix_path,
        my_games_folder,
        profile_path: profile.path.clone(),
        ini_files: game_def.ini_files.clone(),
    })
}

fn build_save_sync_info(state: &AppState) -> Option<SaveSyncInfo> {
    let instance = state.instance.as_ref()?;
    let profile = instance.active_profile.as_ref()?;
    if !profile.local_saves() {
        return None;
    }

    let game_def = GameDef::from_instance(
        instance.game_name(),
        instance.config.game_directory().as_deref(),
    )?;
    let my_games_folder = game_def.my_games_folder.clone()?;

    let prefix_path = if let Some(fluorine) =
        mo2core::fluorine::FluorineConfig::load().filter(|c| c.prefix_exists())
    {
        PathBuf::from(&fluorine.prefix_path)
    } else {
        instance.config.wine_prefix_path()?.to_path_buf()
    };

    Some(SaveSyncInfo {
        prefix_path,
        my_games_folder,
        profile_path: profile.path.clone(),
        save_subdir: mo2core::launcher::wine_prefix::resolve_profile_save_subdir(
            &profile.path,
            &game_def.ini_files,
        ),
    })
}

/// Start a timer that polls every 2 seconds to check if the launched process has exited.
/// When the process exits, unlocks the app state and optionally syncs INI files back.
fn start_process_monitor(
    timer: &Rc<Timer>,
    ui_handle: &slint::Weak<MainWindow>,
    state: &Rc<RefCell<AppState>>,
    pid: u32,
    ini_sync: Option<IniSyncInfo>,
    save_sync: Option<SaveSyncInfo>,
) {
    let ui_handle = ui_handle.clone();
    let state = state.clone();
    let timer_clone = timer.clone();
    timer.start(
        TimerMode::Repeated,
        std::time::Duration::from_secs(2),
        move || {
            if is_process_alive(pid) {
                return; // still running
            }
            // Process exited — unlock and stop polling
            timer_clone.stop();
            tracing::info!("Launched process (pid {pid}) exited — unlocking");

            // Sync INI files back to profile if LocalSettings was enabled
            if let Some(ref sync) = ini_sync {
                if let Ok(prefix) =
                    mo2core::launcher::wine_prefix::WinePrefix::load(&sync.prefix_path)
                {
                    if let Err(e) = mo2core::launcher::wine_prefix::sync_ini_back_to_profile(
                        &prefix,
                        &sync.my_games_folder,
                        &sync.profile_path,
                        &sync.ini_files,
                    ) {
                        tracing::error!("Failed to sync INI files back to profile: {e}");
                    }
                }
            }

            if let Some(ref sync) = save_sync {
                if let Ok(prefix) =
                    mo2core::launcher::wine_prefix::WinePrefix::load(&sync.prefix_path)
                {
                    if let Err(e) = mo2core::launcher::wine_prefix::sync_saves_back_to_profile(
                        &prefix,
                        &sync.my_games_folder,
                        &sync.profile_path,
                        &sync.save_subdir,
                    ) {
                        tracing::error!("Failed to sync save files back to profile: {e}");
                    }
                }
            }

            let mut st = state.borrow_mut();
            st.locked_pid = None;
            let ui = ui_handle.unwrap();
            ui.set_locked(false);
            ui.set_locked_tool_name(SharedString::default());
            // Refresh in case overwrite dir changed during run
            refresh_conflict_cache(&mut st);
            refresh_ui(&ui, &st);
        },
    );
}

/// Mark mod entries with collapse state based on which separators are collapsed.
/// Mods between a collapsed separator and the next separator are hidden.
fn apply_collapse_state(entries: &mut [ModEntry], collapsed: &HashSet<String>) {
    // Precompute whether each separator actually has mods below it.
    let mut has_children_by_separator: HashMap<String, bool> = HashMap::new();
    for i in 0..entries.len() {
        if !entries[i].is_separator {
            continue;
        }

        let mut has_children = false;
        for next in entries.iter().skip(i + 1) {
            if next.is_separator {
                break;
            }
            if !next.is_overwrite {
                has_children = true;
                break;
            }
        }
        has_children_by_separator.insert(entries[i].name.to_string(), has_children);
    }

    let mut in_collapsed_group = false;
    for entry in entries.iter_mut() {
        if entry.is_separator {
            let has_children = has_children_by_separator
                .get(entry.name.as_str())
                .copied()
                .unwrap_or(false);
            entry.separator_has_children = has_children;
            let is_collapsed = has_children && collapsed.contains(entry.name.as_str());
            entry.is_collapsed = is_collapsed;
            entry.hidden_by_collapse = false; // separators are always visible
            in_collapsed_group = is_collapsed;
        } else {
            entry.separator_has_children = false;
            entry.hidden_by_collapse = in_collapsed_group;
        }
    }
}

/// Build Data tab entries from VFS tree.
fn build_data_entries(
    _instance: &Instance,
    vfs_tree: &Option<VfsTree>,
    expanded: &HashSet<String>,
) -> Vec<DataEntry> {
    let Some(ref tree) = vfs_tree else {
        return Vec::new();
    };

    tree.flatten_for_display(expanded)
        .into_iter()
        .map(|e| {
            let has_children = if e.is_directory {
                // Check if directory has children by looking at the tree
                tree.root
                    .resolve(&e.virtual_path)
                    .map(|n| !n.list_children().is_empty())
                    .unwrap_or(false)
            } else {
                false
            };
            DataEntry {
                name: SharedString::from(e.name.as_str()),
                virtual_path: SharedString::from(e.virtual_path.as_str()),
                origin: SharedString::from(e.origin.as_str()),
                is_directory: e.is_directory,
                depth: e.depth as i32,
                size: if e.size > 0 {
                    SharedString::from(download::format_size(e.size).as_str())
                } else {
                    SharedString::default()
                },
                is_expanded: expanded.contains(&e.virtual_path),
                has_children,
            }
        })
        .collect()
}

/// Build Save tab entries.
fn build_save_entries(instance: &Instance) -> Vec<SaveEntry> {
    // Determine save directory
    let save_dir = if let Some(ref profile) = instance.active_profile {
        if profile.local_saves() {
            Some(profile.path.join("saves"))
        } else {
            let game_dir = instance.config.game_directory();
            saves::resolve_save_dir(
                &profile.path,
                false,
                instance.game_name(),
                game_dir.as_deref(),
            )
        }
    } else {
        None
    };

    let Some(dir) = save_dir else {
        return Vec::new();
    };

    let save_infos = saves::scan_saves(&dir).unwrap_or_default();
    save_infos
        .into_iter()
        .map(|s| {
            let date_text = format_system_time(s.modified);
            SaveEntry {
                filename: SharedString::from(s.filename.as_str()),
                character: SharedString::from(s.character.as_str()),
                size_text: SharedString::from(download::format_size(s.size).as_str()),
                date_text: SharedString::from(date_text.as_str()),
            }
        })
        .collect()
}

/// Format a SystemTime for display.
fn format_system_time(time: std::time::SystemTime) -> String {
    match time.duration_since(std::time::UNIX_EPOCH) {
        Ok(dur) => {
            let secs = dur.as_secs();
            let days = secs / 86400;
            let rem = secs % 86400;
            let hours = rem / 3600;
            let mins = (rem % 3600) / 60;

            // Simple date calculation (approximate)
            let years = 1970 + days / 365;
            let day_of_year = days % 365;
            let month = day_of_year / 30 + 1;
            let day = day_of_year % 30 + 1;

            format!(
                "{:04}-{:02}-{:02} {:02}:{:02}",
                years,
                month.min(12),
                day.min(31),
                hours,
                mins
            )
        }
        Err(_) => String::from("Unknown"),
    }
}

/// Build a flat tree of InstallTreeEntry items from a staging directory.
fn build_install_tree(
    staging_dir: &Path,
    data_dir_name: &str,
    layout: &mo2core::install::ContentLayout,
) -> Vec<InstallTreeEntry> {
    let data_root = detect_data_root_path(staging_dir, data_dir_name, layout);
    let expanded = HashSet::new(); // start collapsed
    build_install_tree_internal(staging_dir, &expanded, &data_root)
}

/// Build install tree with given expanded/data state.
fn build_install_tree_with_state(
    staging_dir: &Path,
    expanded: &HashSet<String>,
    data_path: &str,
) -> Vec<InstallTreeEntry> {
    build_install_tree_internal(staging_dir, expanded, data_path)
}

fn build_install_tree_internal(
    staging_dir: &Path,
    expanded: &HashSet<String>,
    data_path: &str,
) -> Vec<InstallTreeEntry> {
    let mut entries = Vec::new();
    let wrapper_dir = detect_single_wrapper_dir(staging_dir);
    let start_dir = wrapper_dir.as_deref().unwrap_or(staging_dir);
    let depth_offset = wrapper_dir
        .as_deref()
        .and_then(|p| p.strip_prefix(staging_dir).ok())
        .map(|p| p.components().count() as i32)
        .unwrap_or(0);
    build_tree_recursive(
        staging_dir,
        start_dir,
        depth_offset,
        expanded,
        data_path,
        &mut entries,
    );
    entries
}

fn build_tree_recursive(
    base: &Path,
    current: &Path,
    depth_offset: i32,
    expanded: &HashSet<String>,
    data_path: &str,
    out: &mut Vec<InstallTreeEntry>,
) {
    let Ok(read_dir) = std::fs::read_dir(current) else {
        return;
    };
    let mut children: Vec<_> = read_dir.filter_map(|e| e.ok()).collect();
    // Directories first, then files, sorted alphabetically
    children.sort_by(|a, b| {
        let a_dir = a.path().is_dir();
        let b_dir = b.path().is_dir();
        b_dir
            .cmp(&a_dir)
            .then_with(|| a.file_name().cmp(&b.file_name()))
    });

    for child in children {
        let child_path = child.path();
        let rel = child_path
            .strip_prefix(base)
            .unwrap_or(&child_path)
            .to_string_lossy()
            .to_string();
        let name = child.file_name().to_string_lossy().to_string();
        let is_dir = child_path.is_dir();
        let raw_depth = rel.matches('/').count() as i32;
        let depth = (raw_depth - depth_offset).max(0);
        let has_children = if is_dir {
            std::fs::read_dir(&child_path)
                .map(|rd| rd.count() > 0)
                .unwrap_or(false)
        } else {
            false
        };
        let is_expanded = expanded.contains(&rel);
        let is_data_root = rel == data_path;

        out.push(InstallTreeEntry {
            name: SharedString::from(name.as_str()),
            path: SharedString::from(rel.as_str()),
            is_directory: is_dir,
            depth,
            checked: true,
            is_expanded,
            has_children,
            is_data_root,
        });

        // Only recurse into expanded directories
        if is_dir && is_expanded {
            build_tree_recursive(base, &child_path, depth_offset, expanded, data_path, out);
        }
    }
}

fn detect_single_wrapper_dir(staging_dir: &Path) -> Option<PathBuf> {
    let entries: Vec<PathBuf> = std::fs::read_dir(staging_dir)
        .ok()?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .collect();

    if entries.len() != 1 {
        return None;
    }

    let only = entries.into_iter().next()?;
    if only.is_dir() {
        Some(only)
    } else {
        None
    }
}

fn find_root_sibling_for_selected_data(staging_dir: &Path, data_path: &str) -> Option<PathBuf> {
    if data_path.is_empty() {
        return None;
    }
    let data_abs = staging_dir.join(data_path);
    let parent = data_abs.parent()?;
    let root_name = find_case_insensitive_name(parent, "Root")?;
    let root_abs = parent.join(root_name);
    if root_abs.is_dir() {
        Some(root_abs)
    } else {
        None
    }
}

/// Detect the data root path within a staging directory based on layout.
fn detect_data_root_path(
    staging_dir: &Path,
    data_dir_name: &str,
    layout: &mo2core::install::ContentLayout,
) -> String {
    match layout {
        mo2core::install::ContentLayout::DataFolder {
            data_dir_name: name,
        } => {
            detect_data_root_path_with_unwrap(staging_dir, name).unwrap_or_else(|| name.to_string())
        }
        mo2core::install::ContentLayout::RootData => {
            // Root of staging IS the data
            String::new()
        }
        mo2core::install::ContentLayout::Fomod
        | mo2core::install::ContentLayout::Bain
        | mo2core::install::ContentLayout::Unknown => {
            detect_data_root_path_with_unwrap(staging_dir, data_dir_name).unwrap_or_default()
        }
    }
}

fn detect_data_root_path_with_unwrap(staging_dir: &Path, data_dir_name: &str) -> Option<String> {
    let mut current = staging_dir.to_path_buf();
    let mut prefix_parts: Vec<String> = Vec::new();

    loop {
        if let Some(found) = find_case_insensitive_name(&current, data_dir_name) {
            if prefix_parts.is_empty() {
                return Some(found);
            }
            let mut all_parts = prefix_parts;
            all_parts.push(found);
            return Some(all_parts.join("/"));
        }

        // Follow common archive wrapper pattern: single top-level folder.
        let mut dirs: Vec<PathBuf> = std::fs::read_dir(&current)
            .ok()?
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| p.is_dir())
            .collect();
        if dirs.len() != 1 {
            return None;
        }
        let next = dirs.remove(0);
        let segment = next.file_name()?.to_string_lossy().to_string();
        prefix_parts.push(segment);
        current = next;
    }
}

fn is_supported_mod_archive(path: &Path) -> bool {
    let Some(ext) = path.extension().and_then(|e| e.to_str()) else {
        return false;
    };
    matches!(
        ext.to_ascii_lowercase().as_str(),
        "zip" | "7z" | "rar" | "tar" | "gz" | "xz" | "bz2" | "tgz" | "tbz2" | "txz"
    )
}

fn derive_mod_name_from_archive(path: &Path) -> String {
    let mut name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or_default()
        .to_string();
    if name.is_empty() {
        return "New Mod".to_string();
    }

    let archive_exts = [
        "zip", "7z", "rar", "tar", "gz", "xz", "bz2", "tgz", "tbz2", "txz",
    ];
    loop {
        let next = Path::new(&name)
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.to_ascii_lowercase())
            .filter(|e| archive_exts.contains(&e.as_str()))
            .and_then(|_| {
                Path::new(&name)
                    .file_stem()
                    .map(|s| s.to_string_lossy().to_string())
            });
        match next {
            Some(stem) if !stem.is_empty() && stem != name => name = stem,
            _ => break,
        }
    }

    if name.is_empty() {
        "New Mod".to_string()
    } else {
        name
    }
}

fn open_install_dialog_for_archive(
    ui: &MainWindow,
    state: &Rc<RefCell<AppState>>,
    archive_path: &Path,
) {
    if !archive_path.exists() || !archive_path.is_file() {
        tracing::warn!(
            "Dropped/selected archive path is not a regular file: {}",
            archive_path.display()
        );
        return;
    }
    if !is_supported_mod_archive(archive_path) {
        tracing::info!(
            "Ignoring unsupported dropped file: {}",
            archive_path.display()
        );
        return;
    }

    // Extract to temp dir
    let staging = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => {
            tracing::error!("Failed to create temp dir: {e}");
            return;
        }
    };
    if let Err(e) = mo2core::install::extract_archive(archive_path, staging.path()) {
        tracing::error!("Failed to extract archive: {e}");
        let _ = rfd::MessageDialog::new()
            .set_title("Extract Failed")
            .set_level(rfd::MessageLevel::Error)
            .set_description(format!(
                "Failed to extract archive:\n{}\n\n{}",
                archive_path.display(),
                e
            ))
            .set_buttons(rfd::MessageButtons::Ok)
            .show();
        return;
    }

    // Detect layout
    let st = state.borrow();
    let data_dir_name = st
        .instance
        .as_ref()
        .map(resolve_data_dir_name)
        .unwrap_or_else(|| "Data".to_string());
    drop(st);

    let layout = mo2core::install::detect_layout(staging.path(), &data_dir_name);

    // Derive mod name from archive filename (handles multi-extension like .tar.gz)
    let mod_name = derive_mod_name_from_archive(archive_path);

    // Build the install tree for the dialog
    let tree_entries = build_install_tree(staging.path(), &data_dir_name, &layout);
    let data_path = detect_data_root_path(staging.path(), &data_dir_name, &layout);
    let valid = !data_path.is_empty()
        && mo2core::install::looks_like_game_data(&staging.path().join(&data_path));

    // Persist the staging dir by leaking it (cleaned up on cancel/confirm)
    let staging_path_str = staging.path().display().to_string();
    let _staging_dir = staging.keep();

    ui.set_install_mod_name(SharedString::from(mod_name.as_str()));
    ui.set_install_tree(ModelRc::new(VecModel::from(tree_entries)));
    ui.set_install_data_valid(valid);
    ui.set_install_data_path(SharedString::from(data_path.as_str()));
    ui.set_install_staging_path(SharedString::from(staging_path_str.as_str()));
    ui.set_show_install_tree_menu(false);
    ui.set_show_install_dialog(true);
}

/// Case-insensitive lookup that returns the actual directory name (not full path).
fn find_case_insensitive_name(dir: &Path, target: &str) -> Option<String> {
    let lower = target.to_lowercase();
    std::fs::read_dir(dir)
        .ok()?
        .filter_map(|e| e.ok())
        .find(|e| e.file_name().to_string_lossy().to_lowercase() == lower)
        .map(|e| e.file_name().to_string_lossy().to_string())
}

/// Recursively copy a directory tree (for mod installation).
fn copy_dir_recursive_install(
    src: &Path,
    dest: &Path,
) -> Result<usize, Box<dyn std::error::Error>> {
    let mut count = 0;
    for entry in walkdir::WalkDir::new(src)
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let rel = entry.path().strip_prefix(src)?;
        let target = dest.join(rel);

        if entry.file_type().is_dir() {
            std::fs::create_dir_all(&target)?;
        } else {
            if let Some(parent) = target.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::copy(entry.path(), &target)?;
            count += 1;
        }
    }
    Ok(count)
}

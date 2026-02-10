//! NaK FFI - C bindings for NaK game detection and Proton management
//!
//! Memory management rules:
//! - Owned strings returned as `*mut c_char` must be freed with `nak_string_free()`
//! - Struct lists (NakGameList, etc.) must be freed with their corresponding `_free()` fn
//! - Error returns: functions returning `*mut c_char` for errors use null = success
//! - `NakKnownGame` pointers are static data and must NOT be freed

use std::ffi::{c_char, c_float, c_int, CStr, CString};
use std::path::Path;
use std::ptr;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, LazyLock, Mutex};

// ============================================================================
// Helper functions
// ============================================================================

fn to_cstring(s: &str) -> *mut c_char {
    CString::new(s).unwrap_or_default().into_raw()
}

fn to_cstring_opt(s: Option<&str>) -> *mut c_char {
    match s {
        Some(s) => to_cstring(s),
        None => ptr::null_mut(),
    }
}

unsafe fn from_cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("")
    }
}

fn error_to_cstring(e: Box<dyn std::error::Error>) -> *mut c_char {
    to_cstring(&e.to_string())
}

// ============================================================================
// Tier 1: Game Detection
// ============================================================================

/// A detected game installation (C-compatible)
#[repr(C)]
pub struct NakGame {
    pub name: *mut c_char,
    pub app_id: *mut c_char,
    pub install_path: *mut c_char,
    pub prefix_path: *mut c_char, // null if no prefix
    pub launcher: *mut c_char,    // display name string
    pub my_games_folder: *mut c_char,
    pub appdata_local_folder: *mut c_char,
    pub appdata_roaming_folder: *mut c_char,
    pub registry_path: *mut c_char,
    pub registry_value: *mut c_char,
}

/// List of detected games
#[repr(C)]
pub struct NakGameList {
    pub games: *mut NakGame,
    pub count: usize,
    pub steam_count: usize,
    pub heroic_count: usize,
    pub bottles_count: usize,
}

#[derive(Clone)]
struct CachedGame {
    name: String,
    app_id: String,
    install_path: String,
    prefix_path: Option<String>,
    launcher: String,
    my_games_folder: Option<String>,
    appdata_local_folder: Option<String>,
    appdata_roaming_folder: Option<String>,
    registry_path: Option<String>,
    registry_value: Option<String>,
}

#[derive(Clone, Default)]
struct CachedGameList {
    games: Vec<CachedGame>,
    steam_count: usize,
    heroic_count: usize,
    bottles_count: usize,
}

static DETECTED_GAMES_CACHE: LazyLock<Mutex<Option<CachedGameList>>> =
    LazyLock::new(|| Mutex::new(None));

fn detect_games_cached() -> CachedGameList {
    let mut cache = DETECTED_GAMES_CACHE.lock().unwrap();
    if let Some(cached) = cache.as_ref() {
        return cached.clone();
    }

    let result = nak_rust::game_finder::detect_all_games();
    let cached = CachedGameList {
        games: result
            .games
            .iter()
            .map(|g| CachedGame {
                name: g.name.clone(),
                app_id: g.app_id.clone(),
                install_path: g.install_path.to_string_lossy().into_owned(),
                prefix_path: g
                    .prefix_path
                    .as_ref()
                    .map(|p| p.to_string_lossy().into_owned()),
                launcher: g.launcher.display_name().to_string(),
                my_games_folder: g.my_games_folder.clone(),
                appdata_local_folder: g.appdata_local_folder.clone(),
                appdata_roaming_folder: g.appdata_roaming_folder.clone(),
                registry_path: g.registry_path.clone(),
                registry_value: g.registry_value.clone(),
            })
            .collect(),
        steam_count: result.steam_count,
        heroic_count: result.heroic_count,
        bottles_count: result.bottles_count,
    };

    *cache = Some(cached.clone());
    cached
}

/// Detect all installed games across all launchers
#[no_mangle]
pub extern "C" fn nak_detect_all_games() -> NakGameList {
    let result = detect_games_cached();

    let mut games: Vec<NakGame> = result
        .games
        .iter()
        .map(|g| NakGame {
            name: to_cstring(&g.name),
            app_id: to_cstring(&g.app_id),
            install_path: to_cstring(&g.install_path),
            prefix_path: match &g.prefix_path {
                Some(p) => to_cstring(p),
                None => ptr::null_mut(),
            },
            launcher: to_cstring(&g.launcher),
            my_games_folder: to_cstring_opt(g.my_games_folder.as_deref()),
            appdata_local_folder: to_cstring_opt(g.appdata_local_folder.as_deref()),
            appdata_roaming_folder: to_cstring_opt(g.appdata_roaming_folder.as_deref()),
            registry_path: to_cstring_opt(g.registry_path.as_deref()),
            registry_value: to_cstring_opt(g.registry_value.as_deref()),
        })
        .collect();

    let list = NakGameList {
        games: games.as_mut_ptr(),
        count: games.len(),
        steam_count: result.steam_count,
        heroic_count: result.heroic_count,
        bottles_count: result.bottles_count,
    };
    std::mem::forget(games);
    list
}

/// Free a NakGameList returned by nak_detect_all_games
#[no_mangle]
pub unsafe extern "C" fn nak_game_list_free(list: NakGameList) {
    if list.games.is_null() {
        return;
    }
    let games = unsafe { Vec::from_raw_parts(list.games, list.count, list.count) };
    for g in games {
        free_if_nonnull(g.name);
        free_if_nonnull(g.app_id);
        free_if_nonnull(g.install_path);
        free_if_nonnull(g.prefix_path);
        free_if_nonnull(g.launcher);
        free_if_nonnull(g.my_games_folder);
        free_if_nonnull(g.appdata_local_folder);
        free_if_nonnull(g.appdata_roaming_folder);
        free_if_nonnull(g.registry_path);
        free_if_nonnull(g.registry_value);
    }
}

unsafe fn free_if_nonnull(p: *mut c_char) {
    if !p.is_null() {
        let _ = unsafe { CString::from_raw(p) };
    }
}

/// A known game definition (static data, do NOT free)
#[repr(C)]
pub struct NakKnownGame {
    pub name: *const c_char,
    pub steam_app_id: *const c_char,
    pub gog_app_id: *const c_char, // null if none
    pub my_games_folder: *const c_char,
    pub appdata_local_folder: *const c_char,
    pub appdata_roaming_folder: *const c_char,
    pub registry_path: *const c_char,
    pub registry_value: *const c_char,
    pub steam_folder: *const c_char,
}

// We need to leak CStrings for the static known games list since the Rust statics
// are &str, not null-terminated. We build the list once and leak it.
// Raw pointers in NakKnownGame prevent Send/Sync, so we wrap in a newtype.
struct KnownGamesVec(Vec<NakKnownGame>);
// SAFETY: The leaked CStrings are effectively 'static and immutable after initialization.
unsafe impl Send for KnownGamesVec {}
unsafe impl Sync for KnownGamesVec {}

static KNOWN_GAMES_FFI: std::sync::LazyLock<KnownGamesVec> = std::sync::LazyLock::new(|| {
    KnownGamesVec(
        nak_rust::game_finder::KNOWN_GAMES
            .iter()
            .map(|kg| NakKnownGame {
                name: leak_str(kg.name),
                steam_app_id: leak_str(kg.steam_app_id),
                gog_app_id: leak_str_opt(kg.gog_app_id),
                my_games_folder: leak_str_opt(kg.my_games_folder),
                appdata_local_folder: leak_str_opt(kg.appdata_local_folder),
                appdata_roaming_folder: leak_str_opt(kg.appdata_roaming_folder),
                registry_path: leak_str(kg.registry_path),
                registry_value: leak_str(kg.registry_value),
                steam_folder: leak_str(kg.steam_folder),
            })
            .collect(),
    )
});

fn leak_str(s: &str) -> *const c_char {
    CString::new(s).unwrap_or_default().into_raw() as *const c_char
}

fn leak_str_opt(s: Option<&str>) -> *const c_char {
    match s {
        Some(s) => leak_str(s),
        None => ptr::null(),
    }
}

/// Get the list of all known games (static data, do NOT free)
///
/// Returns a pointer to the first element and writes the count to `out_count`.
#[no_mangle]
pub unsafe extern "C" fn nak_get_known_games(out_count: *mut usize) -> *const NakKnownGame {
    let games = &KNOWN_GAMES_FFI.0;
    if !out_count.is_null() {
        *out_count = games.len();
    }
    games.as_ptr()
}

// ============================================================================
// Tier 2: Proton Detection
// ============================================================================

/// An installed Proton version (C-compatible)
#[repr(C)]
pub struct NakSteamProton {
    pub name: *mut c_char,
    pub config_name: *mut c_char,
    pub path: *mut c_char,
    pub is_steam_proton: c_int,
    pub is_experimental: c_int,
}

/// List of detected Proton installations
#[repr(C)]
pub struct NakProtonList {
    pub protons: *mut NakSteamProton,
    pub count: usize,
}

/// Find all installed Proton versions
#[no_mangle]
pub extern "C" fn nak_find_steam_protons() -> NakProtonList {
    let protons = nak_rust::steam::find_steam_protons();

    let mut ffi_protons: Vec<NakSteamProton> = protons
        .iter()
        .map(|p| NakSteamProton {
            name: to_cstring(&p.name),
            config_name: to_cstring(&p.config_name),
            path: to_cstring(&p.path.to_string_lossy()),
            is_steam_proton: p.is_steam_proton as c_int,
            is_experimental: p.is_experimental as c_int,
        })
        .collect();

    let list = NakProtonList {
        protons: ffi_protons.as_mut_ptr(),
        count: ffi_protons.len(),
    };
    std::mem::forget(ffi_protons);
    list
}

/// Free a NakProtonList
#[no_mangle]
pub unsafe extern "C" fn nak_proton_list_free(list: NakProtonList) {
    if list.protons.is_null() {
        return;
    }
    let protons = unsafe { Vec::from_raw_parts(list.protons, list.count, list.count) };
    for p in protons {
        free_if_nonnull(p.name);
        free_if_nonnull(p.config_name);
        free_if_nonnull(p.path);
    }
}

// ============================================================================
// Tier 3: Steam Shortcuts
// ============================================================================

/// Result from adding a Steam shortcut
#[repr(C)]
pub struct NakShortcutResult {
    pub app_id: u32,
    pub prefix_path: *mut c_char,
    pub error: *mut c_char, // null on success
}

/// Add a mod manager as a non-Steam game shortcut
///
/// Returns a NakShortcutResult. Check `error` field - null means success.
#[no_mangle]
pub unsafe extern "C" fn nak_add_mod_manager_shortcut(
    name: *const c_char,
    exe_path: *const c_char,
    start_dir: *const c_char,
    proton_name: *const c_char,
) -> NakShortcutResult {
    let name = unsafe { from_cstr(name) };
    let exe = unsafe { from_cstr(exe_path) };
    let dir = unsafe { from_cstr(start_dir) };
    let proton = unsafe { from_cstr(proton_name) };

    match nak_rust::steam::add_mod_manager_shortcut(name, exe, dir, proton, None, false) {
        Ok(result) => NakShortcutResult {
            app_id: result.app_id,
            prefix_path: to_cstring(&result.prefix_path.to_string_lossy()),
            error: ptr::null_mut(),
        },
        Err(e) => NakShortcutResult {
            app_id: 0,
            prefix_path: ptr::null_mut(),
            error: error_to_cstring(e),
        },
    }
}

/// Remove a non-Steam game shortcut by AppID
///
/// Returns null on success, or an error message string (caller must free with nak_string_free).
#[no_mangle]
pub unsafe extern "C" fn nak_remove_steam_shortcut(app_id: u32) -> *mut c_char {
    match nak_rust::steam::remove_steam_shortcut(app_id) {
        Ok(()) => ptr::null_mut(),
        Err(e) => error_to_cstring(e),
    }
}

/// Free a NakShortcutResult
#[no_mangle]
pub unsafe extern "C" fn nak_shortcut_result_free(result: NakShortcutResult) {
    free_if_nonnull(result.prefix_path);
    free_if_nonnull(result.error);
}

// ============================================================================
// Tier 4: Steam Paths
// ============================================================================

/// Find the Steam installation path
///
/// Returns a newly allocated string (caller must free with nak_string_free),
/// or null if Steam is not found.
#[no_mangle]
pub extern "C" fn nak_find_steam_path() -> *mut c_char {
    match nak_rust::steam::find_steam_path() {
        Some(path) => to_cstring(&path.to_string_lossy()),
        None => ptr::null_mut(),
    }
}

// ============================================================================
// Tier 5: Managed Prefixes
// ============================================================================

/// A managed Wine prefix (C-compatible)
#[repr(C)]
pub struct NakManagedPrefix {
    pub app_id: u32,
    pub name: *mut c_char,
    pub prefix_path: *mut c_char,
    pub install_path: *mut c_char,
    pub manager_type: *mut c_char,
    pub library_path: *mut c_char,
    pub created: *mut c_char,
    pub proton_config_name: *mut c_char, // null if not set
}

/// List of managed prefixes
#[repr(C)]
pub struct NakManagedPrefixList {
    pub prefixes: *mut NakManagedPrefix,
    pub count: usize,
}

/// Load all managed prefixes
#[no_mangle]
pub extern "C" fn nak_managed_prefixes_load() -> NakManagedPrefixList {
    let managed = nak_rust::config::ManagedPrefixes::load();

    let mut ffi_prefixes: Vec<NakManagedPrefix> = managed
        .prefixes
        .iter()
        .map(|p| NakManagedPrefix {
            app_id: p.app_id,
            name: to_cstring(&p.name),
            prefix_path: to_cstring(&p.prefix_path),
            install_path: to_cstring(&p.install_path),
            manager_type: to_cstring(&p.manager_type.to_string()),
            library_path: to_cstring(&p.library_path),
            created: to_cstring(&p.created.to_rfc3339()),
            proton_config_name: to_cstring_opt(p.proton_config_name.as_deref()),
        })
        .collect();

    let list = NakManagedPrefixList {
        prefixes: ffi_prefixes.as_mut_ptr(),
        count: ffi_prefixes.len(),
    };
    std::mem::forget(ffi_prefixes);
    list
}

/// Register a new managed prefix
#[no_mangle]
pub unsafe extern "C" fn nak_managed_prefixes_register(
    app_id: u32,
    name: *const c_char,
    prefix_path: *const c_char,
    install_path: *const c_char,
    library_path: *const c_char,
    proton_config_name: *const c_char,
) {
    let name = unsafe { from_cstr(name) };
    let prefix = unsafe { from_cstr(prefix_path) };
    let install = unsafe { from_cstr(install_path) };
    let library = unsafe { from_cstr(library_path) };
    let proton = if proton_config_name.is_null() {
        None
    } else {
        let s = unsafe { from_cstr(proton_config_name) };
        if s.is_empty() {
            None
        } else {
            Some(s)
        }
    };

    nak_rust::config::ManagedPrefixes::register(
        app_id,
        name,
        prefix,
        install,
        nak_rust::config::ManagerType::MO2,
        library,
        proton,
    );
}

/// Unregister a managed prefix by AppID
#[no_mangle]
pub extern "C" fn nak_managed_prefixes_unregister(app_id: u32) {
    nak_rust::config::ManagedPrefixes::unregister(app_id);
}

/// Free a NakManagedPrefixList
#[no_mangle]
pub unsafe extern "C" fn nak_managed_prefix_list_free(list: NakManagedPrefixList) {
    if list.prefixes.is_null() {
        return;
    }
    let prefixes = unsafe { Vec::from_raw_parts(list.prefixes, list.count, list.count) };
    for p in prefixes {
        free_if_nonnull(p.name);
        free_if_nonnull(p.prefix_path);
        free_if_nonnull(p.install_path);
        free_if_nonnull(p.manager_type);
        free_if_nonnull(p.library_path);
        free_if_nonnull(p.created);
        free_if_nonnull(p.proton_config_name);
    }
}

// ============================================================================
// Tier 6: Dependency Installation (callback-based)
// ============================================================================

/// Callback for status messages: fn(message: *const c_char)
pub type NakStatusCallback = Option<unsafe extern "C" fn(*const c_char)>;

/// Callback for log messages: fn(message: *const c_char)
pub type NakLogCallback = Option<unsafe extern "C" fn(*const c_char)>;

/// Callback for progress updates: fn(progress: f32) where 0.0..=1.0
pub type NakProgressCallback = Option<unsafe extern "C" fn(c_float)>;

/// Install all Wine prefix dependencies (winetricks, .NET, registry, etc.)
///
/// This is a blocking call. Use callbacks for progress updates.
/// `cancel_flag` should point to an int that can be set to non-zero to cancel.
///
/// Returns null on success, or an error message (caller must free with nak_string_free).
#[no_mangle]
pub unsafe extern "C" fn nak_install_all_dependencies(
    prefix_path: *const c_char,
    proton_name: *const c_char,
    proton_path: *const c_char,
    status_cb: NakStatusCallback,
    log_cb: NakLogCallback,
    progress_cb: NakProgressCallback,
    cancel_flag: *const c_int,
    app_id: u32,
) -> *mut c_char {
    let prefix = unsafe { from_cstr(prefix_path) };
    let _proton_name = unsafe { from_cstr(proton_name) };
    let proton_path_str = unsafe { from_cstr(proton_path) };

    // Find the matching SteamProton by path
    let protons = nak_rust::steam::find_steam_protons();
    let proton = match protons
        .iter()
        .find(|p| p.path.to_string_lossy() == proton_path_str)
    {
        Some(p) => p.clone(),
        None => {
            return to_cstring(&format!(
                "Proton not found at path: {}",
                proton_path_str
            ));
        }
    };

    // Build cancel flag from raw pointer
    let cancel = Arc::new(AtomicBool::new(false));
    let cancel_clone = cancel.clone();

    // Spawn a thread to poll the C cancel flag
    let cancel_flag_ptr = cancel_flag as usize; // safe to send across threads
    let poll_handle = std::thread::spawn(move || {
        while !cancel_clone.load(Ordering::Relaxed) {
            std::thread::sleep(std::time::Duration::from_millis(100));
            if cancel_flag_ptr != 0 {
                let flag = unsafe { *(cancel_flag_ptr as *const c_int) };
                if flag != 0 {
                    cancel_clone.store(true, Ordering::Relaxed);
                    break;
                }
            }
        }
    });

    let ctx = nak_rust::installers::TaskContext::new(
        move |msg| {
            if let Some(cb) = status_cb {
                let c = CString::new(msg).unwrap_or_default();
                unsafe { cb(c.as_ptr()) };
            }
        },
        move |msg| {
            if let Some(cb) = log_cb {
                let c = CString::new(msg).unwrap_or_default();
                unsafe { cb(c.as_ptr()) };
            }
        },
        move |p| {
            if let Some(cb) = progress_cb {
                unsafe { cb(p) };
            }
        },
        cancel.clone(),
    );

    let result = nak_rust::installers::install_all_dependencies(
        Path::new(prefix),
        &proton,
        &ctx,
        0.0,
        1.0,
        app_id,
    );

    // Stop the cancel polling thread
    cancel.store(true, Ordering::Relaxed);
    let _ = poll_handle.join();

    match result {
        Ok(()) => ptr::null_mut(),
        Err(e) => error_to_cstring(e),
    }
}

/// Apply Wine registry settings to a prefix
///
/// Returns null on success, or an error message (caller must free with nak_string_free).
#[no_mangle]
pub unsafe extern "C" fn nak_apply_wine_registry_settings(
    prefix_path: *const c_char,
    proton_name: *const c_char,
    proton_path: *const c_char,
    log_cb: NakLogCallback,
    app_id: u32,
) -> *mut c_char {
    let prefix = unsafe { from_cstr(prefix_path) };
    let _proton_name = unsafe { from_cstr(proton_name) };
    let proton_path_str = unsafe { from_cstr(proton_path) };

    let protons = nak_rust::steam::find_steam_protons();
    let proton = match protons
        .iter()
        .find(|p| p.path.to_string_lossy() == proton_path_str)
    {
        Some(p) => p.clone(),
        None => {
            return to_cstring(&format!(
                "Proton not found at path: {}",
                proton_path_str
            ));
        }
    };

    let log_fn = move |msg: String| {
        if let Some(cb) = log_cb {
            let c = CString::new(msg).unwrap_or_default();
            unsafe { cb(c.as_ptr()) };
        }
    };

    let app_id_opt = if app_id == 0 { None } else { Some(app_id) };

    match nak_rust::installers::apply_wine_registry_settings(
        Path::new(prefix),
        &proton,
        &log_fn,
        app_id_opt,
    ) {
        Ok(()) => ptr::null_mut(),
        Err(e) => error_to_cstring(e),
    }
}

// ============================================================================
// Tier 7: Prefix Symlinks
// ============================================================================

/// Ensure the Temp directory exists in the Wine prefix's AppData/Local.
///
/// MO2 and other tools require AppData/Local/Temp to exist.
#[no_mangle]
pub unsafe extern "C" fn nak_ensure_temp_directory(prefix_path: *const c_char) {
    let prefix = unsafe { from_cstr(prefix_path) };
    nak_rust::installers::symlinks::ensure_temp_directory(Path::new(prefix));
}

/// Detect installed games and create symlinks from the prefix to game prefixes.
///
/// This is a convenience wrapper that detects games and creates symlinks in one call.
#[no_mangle]
pub unsafe extern "C" fn nak_create_game_symlinks_auto(prefix_path: *const c_char) {
    let prefix = unsafe { from_cstr(prefix_path) };
    nak_rust::installers::symlinks::create_game_symlinks_auto(Path::new(prefix));
}

// ============================================================================
// General: String free
// ============================================================================

/// Free a string returned by any nak_* function
#[no_mangle]
pub unsafe extern "C" fn nak_string_free(s: *mut c_char) {
    free_if_nonnull(s);
}

mod archive;

use std::ffi::{c_char, c_int, CStr, CString};
use std::fs;
use std::path::{Path, PathBuf};
use std::ptr;

use archive::{
    extract_archive_files_batch, list_archive_files, Ba2Builder, Ba2Format, BsaBuilder,
    GameVersion,
};
use walkdir::WalkDir;

#[repr(C)]
pub struct BsaFfiStringList {
    pub items: *mut *mut c_char,
    pub count: usize,
    pub error: *mut c_char,
}

pub type BsaProgressCallback =
    Option<unsafe extern "C" fn(done: u32, total: u32, current_path: *const c_char)>;

fn to_cstring(s: &str) -> *mut c_char {
    CString::new(s).unwrap_or_default().into_raw()
}

fn error_list(msg: &str) -> BsaFfiStringList {
    BsaFfiStringList {
        items: ptr::null_mut(),
        count: 0,
        error: to_cstring(msg),
    }
}

unsafe fn from_cstr<'a>(p: *const c_char) -> Result<&'a str, &'static str> {
    if p.is_null() {
        return Err("null pointer");
    }

    CStr::from_ptr(p)
        .to_str()
        .map_err(|_| "invalid UTF-8 string")
}

fn call_progress(progress_cb: BsaProgressCallback, done: usize, total: usize, path: &str) {
    if let Some(cb) = progress_cb {
        if let Ok(c_path) = CString::new(path) {
            unsafe {
                cb(done as u32, total as u32, c_path.as_ptr());
            }
        }
    }
}

fn path_to_rel(root: &Path, child: &Path) -> anyhow::Result<String> {
    let rel = child.strip_prefix(root)?;
    Ok(rel.to_string_lossy().replace('\\', "/"))
}

fn include_file_for_mode(rel: &str, include_mode: i32) -> bool {
    let is_dds = rel.to_lowercase().ends_with(".dds");
    match include_mode {
        1 => !is_dds,
        2 => is_dds,
        _ => true,
    }
}

#[no_mangle]
pub unsafe extern "C" fn bsa_ffi_list_files(archive_path: *const c_char) -> BsaFfiStringList {
    let archive_path = match from_cstr(archive_path) {
        Ok(v) => v,
        Err(e) => return error_list(e),
    };

    let entries = match list_archive_files(Path::new(archive_path)) {
        Ok(v) => v,
        Err(e) => return error_list(&e.to_string()),
    };

    let mut items: Vec<*mut c_char> = entries.into_iter().map(|e| to_cstring(&e.path)).collect();
    let result = BsaFfiStringList {
        items: items.as_mut_ptr(),
        count: items.len(),
        error: ptr::null_mut(),
    };
    std::mem::forget(items);
    result
}

#[no_mangle]
pub unsafe extern "C" fn bsa_ffi_string_list_free(list: BsaFfiStringList) {
    if !list.items.is_null() {
        let items = Vec::from_raw_parts(list.items, list.count, list.count);
        for p in items {
            if !p.is_null() {
                let _ = CString::from_raw(p);
            }
        }
    }

    if !list.error.is_null() {
        let _ = CString::from_raw(list.error);
    }
}

#[no_mangle]
pub unsafe extern "C" fn bsa_ffi_string_free(s: *mut c_char) {
    if !s.is_null() {
        let _ = CString::from_raw(s);
    }
}

#[no_mangle]
pub unsafe extern "C" fn bsa_ffi_extract_all(
    archive_path: *const c_char,
    output_dir: *const c_char,
    progress_cb: BsaProgressCallback,
    cancel_flag: *const c_int,
) -> *mut c_char {
    let archive_path = match from_cstr(archive_path) {
        Ok(v) => v,
        Err(e) => return to_cstring(e),
    };
    let output_dir = match from_cstr(output_dir) {
        Ok(v) => v,
        Err(e) => return to_cstring(e),
    };

    let archive_path = PathBuf::from(archive_path);
    let output_dir = PathBuf::from(output_dir);

    if let Err(e) = fs::create_dir_all(&output_dir) {
        return to_cstring(&format!("failed to create output directory: {e}"));
    }

    let entries = match list_archive_files(&archive_path) {
        Ok(v) => v,
        Err(e) => return to_cstring(&e.to_string()),
    };

    let total = entries.len();
    let wanted_files: Vec<String> = entries.iter().map(|e| e.path.clone()).collect();
    let progress_count = std::sync::atomic::AtomicUsize::new(0);
    let cancel_addr = cancel_flag as usize;

    let res = extract_archive_files_batch(&archive_path, &wanted_files, |path, data| {
        let cancel_ptr = cancel_addr as *const c_int;
        if !cancel_ptr.is_null() {
            let cancelled = unsafe { *cancel_ptr } != 0;
            if cancelled {
                anyhow::bail!("cancelled");
            }
        }

        let out_path = output_dir.join(path.replace('\\', "/"));
        if let Some(parent) = out_path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&out_path, &data)?;

        let done = progress_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
        call_progress(progress_cb, done, total, path);
        Ok(())
    });

    match res {
        Ok(_) => ptr::null_mut(),
        Err(e) => to_cstring(&e.to_string()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn bsa_ffi_pack_dir(
    input_dir: *const c_char,
    output_archive: *const c_char,
    game_id: *const c_char,
    progress_cb: BsaProgressCallback,
    cancel_flag: *const c_int,
) -> *mut c_char {
    bsa_ffi_pack_dir_filtered(
        input_dir,
        output_archive,
        game_id,
        0,
        progress_cb,
        cancel_flag,
    )
}

#[no_mangle]
pub unsafe extern "C" fn bsa_ffi_pack_dir_filtered(
    input_dir: *const c_char,
    output_archive: *const c_char,
    game_id: *const c_char,
    include_mode: c_int,
    progress_cb: BsaProgressCallback,
    cancel_flag: *const c_int,
) -> *mut c_char {
    let input_dir = match from_cstr(input_dir) {
        Ok(v) => v,
        Err(e) => return to_cstring(e),
    };
    let output_archive = match from_cstr(output_archive) {
        Ok(v) => v,
        Err(e) => return to_cstring(e),
    };
    let game_id = match from_cstr(game_id) {
        Ok(v) => v,
        Err(e) => return to_cstring(e),
    };

    let game = match GameVersion::from_cli_name(game_id) {
        Some(v) => v,
        None => {
            let valid = GameVersion::all()
                .iter()
                .map(GameVersion::cli_name)
                .collect::<Vec<_>>()
                .join(", ");
            return to_cstring(&format!("unknown game_id '{game_id}', valid: {valid}"));
        }
    };

    let input_dir = PathBuf::from(input_dir);
    let output_archive = PathBuf::from(output_archive);

    let mut files: Vec<(String, Vec<u8>)> = Vec::new();
    for entry in WalkDir::new(&input_dir).into_iter().filter_map(|e| e.ok()) {
        if !entry.file_type().is_file() {
            continue;
        }

        if !cancel_flag.is_null() {
            let cancelled = unsafe { *cancel_flag } != 0;
            if cancelled {
                return to_cstring("cancelled");
            }
        }

        let rel = match path_to_rel(&input_dir, entry.path()) {
            Ok(v) => v,
            Err(e) => return to_cstring(&format!("path error: {e}")),
        };

        if !include_file_for_mode(&rel, include_mode) {
            continue;
        }

        let data = match fs::read(entry.path()) {
            Ok(v) => v,
            Err(e) => return to_cstring(&format!("read error: {e}")),
        };

        files.push((rel, data));
    }

    if files.is_empty() {
        return to_cstring("no files found in input_dir");
    }

    let total = files.len();

    if game.is_ba2() {
        let ba2_version = game.ba2_version().unwrap_or_default();
        let compression = game.ba2_compression();

        let format = if output_archive
            .file_name()
            .map(|n| n.to_string_lossy().to_lowercase().contains("textures"))
            .unwrap_or(false)
        {
            Ba2Format::DX10
        } else {
            Ba2Format::General
        };

        let mut builder = Ba2Builder::new()
            .with_version(ba2_version)
            .with_compression(compression)
            .with_format(format);

        for (idx, (rel, data)) in files.into_iter().enumerate() {
            if !cancel_flag.is_null() {
                let cancelled = unsafe { *cancel_flag } != 0;
                if cancelled {
                    return to_cstring("cancelled");
                }
            }
            builder.add_file(&rel, data);
            call_progress(progress_cb, idx + 1, total, &rel);
        }

        match builder.build_with_progress(&output_archive, |_, _, _| {}) {
            Ok(_) => ptr::null_mut(),
            Err(e) => to_cstring(&e.to_string()),
        }
    } else {
        let version = match game.bsa_version() {
            Some(v) => v,
            None => return to_cstring("TES3 BSA writing is not supported yet"),
        };

        let compress = game.supports_compression();

        let mut builder = BsaBuilder::new().with_version(version).with_compression(compress);

        for (idx, (rel, data)) in files.into_iter().enumerate() {
            if !cancel_flag.is_null() {
                let cancelled = unsafe { *cancel_flag } != 0;
                if cancelled {
                    return to_cstring("cancelled");
                }
            }
            builder.add_file(&rel, data);
            call_progress(progress_cb, idx + 1, total, &rel);
        }

        match builder.build_with_progress(&output_archive, |_, _, _| {}) {
            Ok(_) => ptr::null_mut(),
            Err(e) => to_cstring(&e.to_string()),
        }
    }
}

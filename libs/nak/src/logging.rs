//! Logging for Fluorine Manager.
//!
//! All log messages are:
//! 1. Written to `~/.local/share/fluorine/logs/nak.log` (always)
//! 2. Forwarded to an optional callback set via `set_log_callback()` (for MOBase::log)

use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;

/// Signature for the log callback: (level, message)
///
/// Levels: "info", "warning", "error", "install", "action", "download"
type LogCallback = Box<dyn Fn(&str, &str) + Send + Sync>;

static LOG_CALLBACK: Mutex<Option<LogCallback>> = Mutex::new(None);

/// Set the global log callback. Call once at startup from FFI.
pub fn set_log_callback(cb: impl Fn(&str, &str) + Send + Sync + 'static) {
    if let Ok(mut guard) = LOG_CALLBACK.lock() {
        *guard = Some(Box::new(cb));
    }
}

/// Get the log directory path.
fn log_dir() -> PathBuf {
    crate::paths::data_dir().join("logs")
}

/// Get the log file path.
fn log_file_path() -> PathBuf {
    log_dir().join("nak.log")
}

fn emit(level: &str, message: &str) {
    // Write to file (always, even before callback is set)
    write_to_file(level, message);

    // Forward to callback if set
    if let Ok(guard) = LOG_CALLBACK.lock() {
        if let Some(ref cb) = *guard {
            cb(level, message);
        }
    }
}

fn write_to_file(level: &str, message: &str) {
    let path = log_file_path();

    // Ensure log directory exists (only try once per message, cheap no-op if exists)
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }

    // Rotate if over 2MB
    if let Ok(meta) = fs::metadata(&path) {
        if meta.len() > 2 * 1024 * 1024 {
            let old = path.with_extension("log.old");
            let _ = fs::rename(&path, &old);
        }
    }

    let Ok(mut file) = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
    else {
        return;
    };

    let timestamp = chrono::Local::now().format("%H:%M:%S");
    let _ = writeln!(file, "[{}] [{}] {}", timestamp, level.to_uppercase(), message);
}

pub fn log_info(message: &str) {
    emit("info", message);
}

pub fn log_warning(message: &str) {
    emit("warning", message);
}

pub fn log_error(message: &str) {
    emit("error", message);
}

pub fn log_install(message: &str) {
    emit("install", message);
}

pub fn log_action(message: &str) {
    emit("action", message);
}

pub fn log_download(message: &str) {
    emit("download", message);
}

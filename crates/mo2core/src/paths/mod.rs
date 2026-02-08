//! Case-insensitive path handling for Windows paths on Linux
//!
//! MO2 uses Windows-style paths with backslashes.
//! This module handles:
//! - Converting `\` to `/` for Linux filesystem operations
//! - Case-insensitive file lookups (Windows is case-insensitive, Linux is not)
//! - Preserving intended case for output paths
//! - Unicode normalization (NFC) for consistent path matching
//! - CP437 to UTF-8 conversion for legacy Windows archives

use std::path::{Path, PathBuf};
use unicode_normalization::UnicodeNormalization;

/// CP437 to Unicode mapping for bytes 0x80-0xFF
/// Used to convert legacy DOS/Windows filenames to UTF-8
const CP437_TO_UNICODE: [char; 128] = [
    'Ç', 'ü', 'é', 'â', 'ä', 'à', 'å', 'ç', 'ê', 'ë', 'è', 'ï', 'î', 'ì', 'Ä', 'Å', 'É', 'æ', 'Æ',
    'ô', 'ö', 'ò', 'û', 'ù', 'ÿ', 'Ö', 'Ü', '¢', '£', '¥', '₧', 'ƒ', 'á', 'í', 'ó', 'ú', 'ñ', 'Ñ',
    'ª', 'º', '¿', '⌐', '¬', '½', '¼', '¡', '«', '»', '░', '▒', '▓', '│', '┤', '╡', '╢', '╖', '╕',
    '╣', '║', '╗', '╝', '╜', '╛', '┐', '└', '┴', '┬', '├', '─', '┼', '╞', '╟', '╚', '╔', '╩', '╦',
    '╠', '═', '╬', '╧', '╨', '╤', '╥', '╙', '╘', '╒', '╓', '╫', '╪', '┘', '┌', '█', '▄', '▌', '▐',
    '▀', 'α', 'ß', 'Γ', 'π', 'Σ', 'σ', 'µ', 'τ', 'Φ', 'Θ', 'Ω', 'δ', '∞', 'φ', 'ε', '∩', '≡', '±',
    '≥', '≤', '⌠', '⌡', '÷', '≈', '°', '∙', '·', '√', 'ⁿ', '²', '■', ' ',
];

/// Convert a byte sequence that might contain CP437 characters to UTF-8
pub fn cp437_to_utf8(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|&b| {
            if b < 0x80 {
                b as char
            } else {
                CP437_TO_UNICODE[(b - 0x80) as usize]
            }
        })
        .collect()
}

/// Convert Windows path separators to Linux
/// `Data\Textures\armor.dds` -> `Data/Textures/armor.dds`
pub fn to_linux_path(path: &str) -> String {
    path.replace('\\', "/")
}

/// Convert a Windows-style path to a native PathBuf
pub fn to_native_pathbuf(path: &str) -> PathBuf {
    PathBuf::from(to_linux_path(path))
}

/// Normalize a path for lookups and comparisons (NFC normalized, lowercase, forward slashes, trimmed)
pub fn normalize_for_lookup(path: &str) -> String {
    path.nfc()
        .collect::<String>()
        .to_lowercase()
        .replace('\\', "/")
        .trim_matches('/')
        .to_string()
}

/// Check if two paths are equal (case-insensitive)
pub fn paths_equal(a: &str, b: &str) -> bool {
    normalize_for_lookup(a) == normalize_for_lookup(b)
}

/// Find a file case-insensitively within a directory
pub fn resolve_case_insensitive(base: &Path, relative: &str) -> Option<PathBuf> {
    let components: Vec<&str> = relative
        .split(['\\', '/'])
        .filter(|s| !s.is_empty())
        .collect();

    if components.is_empty() {
        return Some(base.to_path_buf());
    }

    let mut current = base.to_path_buf();

    for component in components {
        let target_lower = component.nfc().collect::<String>().to_lowercase();

        let found = std::fs::read_dir(&current).ok()?.find_map(|entry| {
            let entry = entry.ok()?;
            let name = entry.file_name();
            let name_str = name.to_string_lossy();
            let name_normalized = name_str.nfc().collect::<String>().to_lowercase();

            if name_normalized == target_lower {
                Some(entry.path())
            } else {
                None
            }
        });

        match found {
            Some(path) => current = path,
            None => return None,
        }
    }

    Some(current)
}

/// Get the parent directory of a path (handles both / and \)
pub fn parent_path(path: &str) -> Option<&str> {
    path.rfind(['\\', '/']).map(|idx| &path[..idx])
}

/// Get the filename from a path (handles both / and \)
pub fn file_name(path: &str) -> &str {
    path.rfind(['\\', '/'])
        .map(|idx| &path[idx + 1..])
        .unwrap_or(path)
}

/// Get file extension (lowercase)
pub fn extension(path: &str) -> Option<&str> {
    let name = file_name(path);
    name.rfind('.').map(|idx| &name[idx + 1..])
}

/// Find a file in a list of archive entries case-insensitively
pub fn find_in_archive_entries<'a>(entries: &'a [String], target: &str) -> Option<&'a str> {
    let target_normalized = normalize_for_lookup(target);
    entries
        .iter()
        .find(|e| normalize_for_lookup(e) == target_normalized)
        .map(|s| s.as_str())
}

/// Convert a Wine Z: drive path to a Linux path.
/// `Z:\home\user\file` → `/home/user/file`
/// Passes through paths that are already Linux format.
pub fn wine_to_linux(path: &str) -> String {
    // Z:\... or z:\...
    if path.starts_with("Z:\\")
        || path.starts_with("z:\\")
        || path.starts_with("Z:/")
        || path.starts_with("z:/")
    {
        let rest = &path[2..]; // skip "Z:" portion
        rest.replace('\\', "/")
    } else if path.starts_with('/') {
        // Already a Linux path
        path.to_string()
    } else {
        // Some other Windows drive or relative path — just normalize separators
        path.replace('\\', "/")
    }
}

/// Convert a Linux path to a Wine Z: drive path.
/// `/home/user/file` → `Z:/home/user/file`
pub fn linux_to_wine(path: &str) -> String {
    if path.starts_with('/') {
        format!("Z:{}", path)
    } else {
        // Already a Windows-style path or relative path
        path.to_string()
    }
}

/// Resolve a Wine C: drive path to a real Linux path using the Wine prefix.
/// `C:\users\user\file` → `<prefix>/drive_c/users/user/file`
pub fn resolve_wine_c_drive(path: &str, wine_prefix: &Path) -> PathBuf {
    // Check for C:\ or C:/
    if path.starts_with("C:\\")
        || path.starts_with("c:\\")
        || path.starts_with("C:/")
        || path.starts_with("c:/")
    {
        let rest = &path[3..]; // skip "C:\" or "C:/"
        let linux_rest = rest.replace('\\', "/");
        wine_prefix.join("drive_c").join(linux_rest)
    } else {
        // Not a C: path — return as-is converted to PathBuf
        PathBuf::from(to_linux_path(path))
    }
}

/// Normalize any path format (Wine Z:, Wine C:, Linux) to a Linux path.
/// If wine_prefix is provided, C: drive paths are resolved through the prefix.
pub fn normalize_any_path(path: &str, wine_prefix: Option<&Path>) -> String {
    if path.starts_with("Z:\\")
        || path.starts_with("z:\\")
        || path.starts_with("Z:/")
        || path.starts_with("z:/")
    {
        wine_to_linux(path)
    } else if (path.starts_with("C:\\")
        || path.starts_with("c:\\")
        || path.starts_with("C:/")
        || path.starts_with("c:/"))
        && wine_prefix.is_some()
    {
        resolve_wine_c_drive(path, wine_prefix.unwrap())
            .display()
            .to_string()
    } else if path.starts_with('/') {
        path.to_string()
    } else {
        // Unknown format, just normalize separators
        to_linux_path(path)
    }
}

/// Create parent directories for a path if they don't exist
pub fn ensure_parent_dirs(path: &Path) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        if !parent.exists() {
            std::fs::create_dir_all(parent)?;
        }
    }
    Ok(())
}

/// Join a base path with a Windows-style relative path
pub fn join_windows_path(base: &Path, relative: &str) -> PathBuf {
    base.join(to_linux_path(relative))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_to_linux_path() {
        assert_eq!(
            to_linux_path("Data\\Textures\\armor.dds"),
            "Data/Textures/armor.dds"
        );
        assert_eq!(to_linux_path("already/linux/path"), "already/linux/path");
        assert_eq!(to_linux_path("mixed\\path/style"), "mixed/path/style");
    }

    #[test]
    fn test_normalize() {
        assert_eq!(
            normalize_for_lookup("Data\\Textures\\Armor.dds"),
            "data/textures/armor.dds"
        );
        assert_eq!(
            normalize_for_lookup("MESHES/Actor/Character"),
            "meshes/actor/character"
        );
    }

    #[test]
    fn test_paths_equal() {
        assert!(paths_equal(
            "Data\\Textures\\armor.dds",
            "data\\textures\\ARMOR.DDS"
        ));
        assert!(!paths_equal(
            "Data\\Textures\\armor.dds",
            "data\\textures\\sword.dds"
        ));
    }

    #[test]
    fn test_file_name() {
        assert_eq!(file_name("Data\\Textures\\armor.dds"), "armor.dds");
        assert_eq!(file_name("armor.dds"), "armor.dds");
        assert_eq!(file_name("Data/Textures/armor.dds"), "armor.dds");
    }

    #[test]
    fn test_extension() {
        assert_eq!(extension("armor.dds"), Some("dds"));
        assert_eq!(extension("Data\\armor.dds"), Some("dds"));
        assert_eq!(extension("noext"), None);
    }

    #[test]
    fn test_parent_path() {
        assert_eq!(
            parent_path("Data\\Textures\\armor.dds"),
            Some("Data\\Textures")
        );
        assert_eq!(parent_path("armor.dds"), None);
    }

    #[test]
    fn test_cp437_to_utf8() {
        let cp437_bytes = b"at\xa3lg gro-larg\xa3m";
        let utf8_result = cp437_to_utf8(cp437_bytes);
        assert_eq!(utf8_result, "atúlg gro-largúm");
    }

    #[test]
    fn test_unicode_normalization() {
        let precomposed = "atúlg gro-largúm";
        let decomposed = "atu\u{0301}lg gro-largu\u{0301}m";
        assert_eq!(
            normalize_for_lookup(precomposed),
            normalize_for_lookup(decomposed)
        );
    }

    #[test]
    fn test_wine_to_linux() {
        assert_eq!(
            wine_to_linux("Z:\\home\\user\\game\\data"),
            "/home/user/game/data"
        );
        assert_eq!(
            wine_to_linux("z:\\home\\user\\file.txt"),
            "/home/user/file.txt"
        );
        assert_eq!(wine_to_linux("Z:/home/user/file"), "/home/user/file");
        // Pass through Linux paths
        assert_eq!(wine_to_linux("/home/user/file"), "/home/user/file");
        // Other drive letters just normalize separators
        assert_eq!(wine_to_linux("D:\\games\\skyrim"), "D:/games/skyrim");
    }

    #[test]
    fn test_linux_to_wine() {
        assert_eq!(linux_to_wine("/home/user/file"), "Z:/home/user/file");
        assert_eq!(
            linux_to_wine("/opt/games/skyrim/Data"),
            "Z:/opt/games/skyrim/Data"
        );
        // Non-Linux paths pass through
        assert_eq!(linux_to_wine("relative/path"), "relative/path");
        assert_eq!(linux_to_wine("C:\\something"), "C:\\something");
    }

    #[test]
    fn test_resolve_wine_c_drive() {
        let prefix = Path::new("/home/user/.wine");
        assert_eq!(
            resolve_wine_c_drive("C:\\users\\user\\Documents", prefix),
            PathBuf::from("/home/user/.wine/drive_c/users/user/Documents")
        );
        assert_eq!(
            resolve_wine_c_drive("c:\\Program Files\\Game", prefix),
            PathBuf::from("/home/user/.wine/drive_c/Program Files/Game")
        );
        // Non-C: paths converted as-is
        assert_eq!(
            resolve_wine_c_drive("/home/user/file", prefix),
            PathBuf::from("/home/user/file")
        );
    }

    #[test]
    fn test_normalize_any_path() {
        let prefix = Path::new("/home/user/.wine");

        // Z: drive
        assert_eq!(
            normalize_any_path("Z:\\home\\user\\file", Some(prefix)),
            "/home/user/file"
        );
        // C: drive with prefix
        assert_eq!(
            normalize_any_path("C:\\users\\user\\file", Some(prefix)),
            "/home/user/.wine/drive_c/users/user/file"
        );
        // C: drive without prefix — just normalizes separators
        assert_eq!(
            normalize_any_path("C:\\users\\user\\file", None),
            "C:/users/user/file"
        );
        // Linux path passes through
        assert_eq!(
            normalize_any_path("/home/user/file", Some(prefix)),
            "/home/user/file"
        );
        // Relative path
        assert_eq!(
            normalize_any_path("Data\\Textures\\file.dds", None),
            "Data/Textures/file.dds"
        );
    }
}

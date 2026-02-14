//! BSA/BA2 (Bethesda Archive) handling
//!
//! Provides read/write support for:
//! - TES3 format BSA files (Morrowind)
//! - TES4 format BSA files (Oblivion, FO3, FNV, Skyrim)
//! - FO4 format BA2 files (Fallout 4, Fallout 76, Starfield)

mod ba2_reader;
mod ba2_writer;
mod reader;
mod tes3_reader;
mod writer;

pub use reader::{
    extract_file, extract_files_batch as extract_bsa_files_batch, list_files, BsaFileEntry,
};
pub use writer::BsaBuilder;

// TES3 (Morrowind) support
pub use tes3_reader::{
    extract_file as extract_tes3_file, extract_files_batch as extract_tes3_files_batch,
    list_files as list_tes3_files,
};

// BA2 support for Fallout 4/Starfield
pub use ba2_reader::{
    extract_file as extract_ba2_file, extract_files_batch as extract_ba2_files_batch,
    list_files as list_ba2_files,
};
pub use ba2_writer::{Ba2Builder, Ba2CompressionFormat, Ba2Format, Ba2Version};

use anyhow::{bail, Result};
use ba2::tes4::{ArchiveFlags, ArchiveTypes, Version};
use ba2::{guess_format, FileFormat, Reader};
use std::collections::HashSet;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use tracing::debug;

/// Archive format type
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArchiveFormat {
    /// TES3 BSA (Morrowind)
    Tes3Bsa,
    /// TES4 BSA (Oblivion, FO3, FNV, Skyrim)
    Bsa,
    /// FO4 BA2 (Fallout 4, Fallout 76, Starfield)
    Ba2,
}

/// Detect archive format using ba2 crate's guess_format
pub fn detect_format(path: &Path) -> Option<ArchiveFormat> {
    // Use ba2's built-in format detection
    if let Ok(file) = File::open(path) {
        let mut reader = BufReader::new(file);
        if let Some(format) = guess_format(&mut reader) {
            let result = match format {
                FileFormat::TES3 => ArchiveFormat::Tes3Bsa,
                FileFormat::TES4 => ArchiveFormat::Bsa,
                FileFormat::FO4 => ArchiveFormat::Ba2,
            };
            debug!("Detected {:?} format for: {}", result, path.display());
            return Some(result);
        }
    }

    // Fall back to extension
    let ext = path.extension()?.to_str()?.to_lowercase();
    match ext.as_str() {
        "bsa" => {
            debug!(
                "Detected BSA by extension (assuming TES4): {}",
                path.display()
            );
            Some(ArchiveFormat::Bsa)
        }
        "ba2" => {
            debug!("Detected BA2 by extension: {}", path.display());
            Some(ArchiveFormat::Ba2)
        }
        _ => None,
    }
}

/// Universal archive file entry
#[derive(Debug, Clone)]
pub struct ArchiveFileEntry {
    pub path: String,
}

/// List files from any Bethesda archive (TES3 BSA, TES4 BSA, or BA2)
pub fn list_archive_files(archive_path: &Path) -> Result<Vec<ArchiveFileEntry>> {
    match detect_format(archive_path) {
        Some(ArchiveFormat::Tes3Bsa) => {
            let files = list_tes3_files(archive_path)?;
            Ok(files
                .into_iter()
                .map(|f| ArchiveFileEntry { path: f.path })
                .collect())
        }
        Some(ArchiveFormat::Bsa) => {
            let files = list_files(archive_path)?;
            Ok(files
                .into_iter()
                .map(|f| ArchiveFileEntry { path: f.path })
                .collect())
        }
        Some(ArchiveFormat::Ba2) => {
            let files = list_ba2_files(archive_path)?;
            Ok(files
                .into_iter()
                .map(|f| ArchiveFileEntry { path: f.path })
                .collect())
        }
        None => bail!("Unknown archive format: {}", archive_path.display()),
    }
}

/// Extract a file from any Bethesda archive (TES3 BSA, TES4 BSA, or BA2)
#[allow(dead_code)]
pub fn extract_archive_file(archive_path: &Path, file_path: &str) -> Result<Vec<u8>> {
    let format = detect_format(archive_path);
    debug!(
        "extract_archive_file: archive={}, file={}, format={:?}",
        archive_path.display(),
        file_path,
        format
    );
    match format {
        Some(ArchiveFormat::Tes3Bsa) => extract_tes3_file(archive_path, file_path),
        Some(ArchiveFormat::Bsa) => extract_file(archive_path, file_path),
        Some(ArchiveFormat::Ba2) => extract_ba2_file(archive_path, file_path),
        None => bail!("Unknown archive format: {}", archive_path.display()),
    }
}

/// Extract multiple files from any Bethesda archive in a single pass.
/// Opens the archive once and calls the callback for each extracted file.
/// `wanted_files` should contain the original paths (as returned by list_archive_files).
/// Returns the number of files successfully extracted.
pub fn extract_archive_files_batch<F>(
    archive_path: &Path,
    wanted_files: &[String],
    callback: F,
) -> Result<usize>
where
    F: Fn(&str, Vec<u8>) -> Result<()> + Send + Sync,
{
    let format = detect_format(archive_path);
    match format {
        Some(ArchiveFormat::Tes3Bsa) => {
            let wanted: HashSet<String> = wanted_files.iter().map(|p| p.to_lowercase()).collect();
            extract_tes3_files_batch(archive_path, &wanted, callback)
        }
        Some(ArchiveFormat::Bsa) => {
            // BSA uses backslash-separated paths
            let wanted: HashSet<String> = wanted_files
                .iter()
                .map(|p| p.replace('/', "\\").to_lowercase())
                .collect();
            extract_bsa_files_batch(archive_path, &wanted, callback)
        }
        Some(ArchiveFormat::Ba2) => {
            // BA2 uses forward-slash paths
            let wanted: HashSet<String> = wanted_files
                .iter()
                .map(|p| p.replace('\\', "/").to_lowercase())
                .collect();
            extract_ba2_files_batch(archive_path, &wanted, callback)
        }
        None => bail!("Unknown archive format: {}", archive_path.display()),
    }
}

/// Game version for archive creation
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum GameVersion {
    /// TES3 BSA (Morrowind) - no compression
    Morrowind,
    /// TES4 v103 (Oblivion) - no compression
    Oblivion,
    /// TES4 v104 (Fallout 3) - zlib compression
    Fallout3,
    /// TES4 v104 (Fallout: New Vegas) - zlib compression
    FalloutNewVegas,
    /// TES4 v104 (Skyrim LE) - zlib compression
    SkyrimLE,
    /// TES4 v105 (Skyrim SE) - zlib compression
    SkyrimSE,
    /// BA2 v1 (Fallout 4 / Fallout 76) - zlib compression
    #[default]
    Fallout4Fo76,
    /// BA2 v7 (Fallout 4 Next Gen) - zlib compression
    Fallout4NGv7,
    /// BA2 v8 (Fallout 4 Next Gen) - zlib compression
    Fallout4NGv8,
    /// BA2 v2 (Starfield) - LZ4 compression
    StarfieldV2,
    /// BA2 v3 (Starfield) - LZ4 compression
    StarfieldV3,
}

#[allow(dead_code)]
impl GameVersion {
    /// Get display name for this game version
    pub fn display_name(&self) -> &'static str {
        match self {
            GameVersion::Morrowind => "Morrowind (BSA)",
            GameVersion::Oblivion => "Oblivion (BSA v103)",
            GameVersion::Fallout3 => "Fallout 3 (BSA v104)",
            GameVersion::FalloutNewVegas => "Fallout: New Vegas (BSA v104)",
            GameVersion::SkyrimLE => "Skyrim LE (BSA v104)",
            GameVersion::SkyrimSE => "Skyrim SE (BSA v105)",
            GameVersion::Fallout4Fo76 => "Fallout 4 / Fallout 76 (BA2 v1)",
            GameVersion::Fallout4NGv7 => "Fallout 4 Next Gen (BA2 v7)",
            GameVersion::Fallout4NGv8 => "Fallout 4 Next Gen (BA2 v8)",
            GameVersion::StarfieldV2 => "Starfield (BA2 v2)",
            GameVersion::StarfieldV3 => "Starfield (BA2 v3)",
        }
    }

    /// Check if this game uses BA2 format
    pub fn is_ba2(&self) -> bool {
        matches!(
            self,
            GameVersion::Fallout4Fo76
                | GameVersion::Fallout4NGv7
                | GameVersion::Fallout4NGv8
                | GameVersion::StarfieldV2
                | GameVersion::StarfieldV3
        )
    }

    /// Check if this game uses TES3 format (Morrowind)
    pub fn is_tes3(&self) -> bool {
        matches!(self, GameVersion::Morrowind)
    }

    /// Check if compression is supported for this game
    pub fn supports_compression(&self) -> bool {
        !matches!(self, GameVersion::Morrowind | GameVersion::Oblivion)
    }

    /// Get BSA version for TES4 format games
    pub fn bsa_version(&self) -> Option<Version> {
        match self {
            GameVersion::Oblivion => Some(Version::v103),
            GameVersion::Fallout3 | GameVersion::FalloutNewVegas | GameVersion::SkyrimLE => {
                Some(Version::v104)
            }
            GameVersion::SkyrimSE => Some(Version::v105),
            _ => None,
        }
    }

    /// Get BA2 version for FO4/Starfield format games
    pub fn ba2_version(&self) -> Option<Ba2Version> {
        match self {
            GameVersion::Fallout4Fo76 => Some(Ba2Version::V1),
            GameVersion::Fallout4NGv7 => Some(Ba2Version::V7),
            GameVersion::Fallout4NGv8 => Some(Ba2Version::V8),
            GameVersion::StarfieldV2 => Some(Ba2Version::V2),
            GameVersion::StarfieldV3 => Some(Ba2Version::V3),
            _ => None,
        }
    }

    /// Get BA2 compression format for this game
    pub fn ba2_compression(&self) -> Ba2CompressionFormat {
        match self {
            GameVersion::StarfieldV2 | GameVersion::StarfieldV3 => Ba2CompressionFormat::Lz4,
            _ => Ba2CompressionFormat::Zlib,
        }
    }

    /// Get all game versions
    pub fn all() -> &'static [GameVersion] {
        &[
            GameVersion::Morrowind,
            GameVersion::Oblivion,
            GameVersion::Fallout3,
            GameVersion::FalloutNewVegas,
            GameVersion::SkyrimLE,
            GameVersion::SkyrimSE,
            GameVersion::Fallout4Fo76,
            GameVersion::Fallout4NGv7,
            GameVersion::Fallout4NGv8,
            GameVersion::StarfieldV2,
            GameVersion::StarfieldV3,
        ]
    }

    /// Convert index to game version
    pub fn from_index(index: i32) -> GameVersion {
        match index {
            0 => GameVersion::Morrowind,
            1 => GameVersion::Oblivion,
            2 => GameVersion::Fallout3,
            3 => GameVersion::FalloutNewVegas,
            4 => GameVersion::SkyrimLE,
            5 => GameVersion::SkyrimSE,
            6 => GameVersion::Fallout4Fo76,
            7 => GameVersion::Fallout4NGv7,
            8 => GameVersion::Fallout4NGv8,
            9 => GameVersion::StarfieldV2,
            10 => GameVersion::StarfieldV3,
            _ => GameVersion::Fallout4Fo76,
        }
    }

    /// Convert game version to index
    pub fn index(self) -> i32 {
        match self {
            GameVersion::Morrowind => 0,
            GameVersion::Oblivion => 1,
            GameVersion::Fallout3 => 2,
            GameVersion::FalloutNewVegas => 3,
            GameVersion::SkyrimLE => 4,
            GameVersion::SkyrimSE => 5,
            GameVersion::Fallout4Fo76 => 6,
            GameVersion::Fallout4NGv7 => 7,
            GameVersion::Fallout4NGv8 => 8,
            GameVersion::StarfieldV2 => 9,
            GameVersion::StarfieldV3 => 10,
        }
    }

    /// Short CLI-friendly name
    pub fn cli_name(&self) -> &'static str {
        match self {
            GameVersion::Morrowind => "morrowind",
            GameVersion::Oblivion => "oblivion",
            GameVersion::Fallout3 => "fo3",
            GameVersion::FalloutNewVegas => "fonv",
            GameVersion::SkyrimLE => "skyrimle",
            GameVersion::SkyrimSE => "skyrimse",
            GameVersion::Fallout4Fo76 => "fo4-fo76",
            GameVersion::Fallout4NGv7 => "fo4ng-v7",
            GameVersion::Fallout4NGv8 => "fo4ng-v8",
            GameVersion::StarfieldV2 => "starfield-v2",
            GameVersion::StarfieldV3 => "starfield-v3",
        }
    }

    /// Parse from CLI name (case-insensitive)
    pub fn from_cli_name(name: &str) -> Option<GameVersion> {
        let lower = name.to_lowercase();
        GameVersion::all()
            .iter()
            .find(|v| v.cli_name() == lower)
            .copied()
    }
}

/// Detect game version from archive format
#[allow(dead_code)]
pub fn detect_game_version(archive_path: &Path) -> Option<GameVersion> {
    match detect_format(archive_path) {
        Some(ArchiveFormat::Tes3Bsa) => Some(GameVersion::Morrowind),
        Some(ArchiveFormat::Ba2) => Some(GameVersion::Fallout4Fo76), // Default to FO4/FO76
        Some(ArchiveFormat::Bsa) => {
            // Try to detect version from BSA header
            let result: Result<(ba2::tes4::Archive, ba2::tes4::ArchiveOptions), _> =
                ba2::tes4::Archive::read(archive_path);
            if let Ok((_, options)) = result {
                match options.version() {
                    Version::v103 => Some(GameVersion::Oblivion),
                    Version::v104 => Some(GameVersion::Fallout3), // Default for v104
                    Version::v105 => Some(GameVersion::SkyrimSE),
                }
            } else {
                Some(GameVersion::Fallout3) // Default
            }
        }
        None => None,
    }
}

/// Default flags for FO3/FNV BSAs
pub fn default_flags_fo3() -> ArchiveFlags {
    ArchiveFlags::DIRECTORY_STRINGS
        | ArchiveFlags::FILE_STRINGS
        | ArchiveFlags::COMPRESSED
        | ArchiveFlags::RETAIN_DIRECTORY_NAMES
        | ArchiveFlags::RETAIN_FILE_NAMES
        | ArchiveFlags::RETAIN_FILE_NAME_OFFSETS
}

/// Default flags for Oblivion BSAs (no compression)
#[allow(dead_code)]
pub fn default_flags_oblivion() -> ArchiveFlags {
    ArchiveFlags::DIRECTORY_STRINGS | ArchiveFlags::FILE_STRINGS
}

/// Detect archive types from BSA name
#[allow(dead_code)]
pub fn detect_types(name: &str) -> ArchiveTypes {
    let name_lower = name.to_lowercase();

    if name_lower.contains("meshes") {
        ArchiveTypes::MESHES
    } else if name_lower.contains("textures") {
        ArchiveTypes::TEXTURES
    } else if name_lower.contains("menuvoices") {
        ArchiveTypes::MENUS | ArchiveTypes::VOICES
    } else if name_lower.contains("voices") {
        ArchiveTypes::VOICES
    } else if name_lower.contains("sound") {
        ArchiveTypes::SOUNDS
    } else {
        ArchiveTypes::MISC
    }
}

/// Detect BSA version from archive name
#[allow(dead_code)]
pub fn detect_version(name: &str) -> Version {
    let name_lower = name.to_lowercase();

    // Oblivion uses v103
    if name_lower.contains("oblivion")
        || name_lower.contains("shiveringisles")
        || name_lower.contains("dlcshiveringisles")
        || name_lower.contains("dlcbattlehorn")
        || name_lower.contains("dlcfrostcrag")
        || name_lower.contains("dlchorse")
        || name_lower.contains("dlcorrery")
        || name_lower.contains("dlcthievesden")
        || name_lower.contains("dlcvilelair")
        || name_lower.contains("knights")
    {
        Version::v103
    } else {
        // Default to FO3/FNV
        Version::v104
    }
}

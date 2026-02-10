//! BSA reading with parallel extraction

use anyhow::{bail, Context, Result};
use ba2::tes4::{Archive, File as BsaFile, FileCompressionOptions};
use ba2::{ByteSlice, Reader};
use rayon::prelude::*;
use std::collections::HashSet;
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use tracing::debug;

/// Entry for a file in a BSA archive
#[derive(Debug, Clone)]
pub struct BsaFileEntry {
    pub path: String,
}

/// List all files in a BSA archive
pub fn list_files(bsa_path: &Path) -> Result<Vec<BsaFileEntry>> {
    let (archive, _): (Archive, _) = Archive::read(bsa_path)
        .with_context(|| format!("Failed to open BSA: {}", bsa_path.display()))?;

    let mut files = Vec::new();

    for (dir_key, folder) in archive.iter() {
        let dir_name = String::from_utf8_lossy(dir_key.name().as_bytes());

        for (file_key, _file) in folder.iter() {
            let file_name = String::from_utf8_lossy(file_key.name().as_bytes());

            // Build full path with backslash (BSA convention)
            let full_path = if dir_name.is_empty() || dir_name == "." {
                file_name.to_string()
            } else {
                format!("{}\\{}", dir_name, file_name)
            };

            files.push(BsaFileEntry { path: full_path });
        }
    }

    debug!("Listed {} files in BSA {}", files.len(), bsa_path.display());
    Ok(files)
}

/// Extract a single file from a BSA archive
#[allow(dead_code)]
pub fn extract_file(bsa_path: &Path, file_path: &str) -> Result<Vec<u8>> {
    let (archive, options): (Archive, _) = Archive::read(bsa_path)
        .with_context(|| format!("Failed to open BSA: {}", bsa_path.display()))?;

    // Convert archive options to compression options (includes version info)
    let compression_options: FileCompressionOptions = (&options).into();

    // Normalize to backslashes and split
    let normalized = file_path.replace('/', "\\");
    let (dir_name, file_name) = if let Some(idx) = normalized.rfind('\\') {
        (&normalized[..idx], &normalized[idx + 1..])
    } else {
        ("", normalized.as_str())
    };

    // Search case-insensitively
    for (dir_key, folder) in archive.iter() {
        let current_dir = String::from_utf8_lossy(dir_key.name().as_bytes());

        if current_dir.eq_ignore_ascii_case(dir_name) {
            for (file_key, file) in folder.iter() {
                let current_file = String::from_utf8_lossy(file_key.name().as_bytes());

                if current_file.eq_ignore_ascii_case(file_name) {
                    // Extract with decompression if needed (uses version from archive options)
                    let data = if file.is_decompressed() {
                        file.as_bytes().to_vec()
                    } else {
                        file.decompress(&compression_options)?.as_bytes().to_vec()
                    };
                    return Ok(data);
                }
            }
        }
    }

    bail!(
        "File not found in BSA: {} (dir='{}', file='{}')",
        file_path,
        dir_name,
        file_name
    )
}

/// Extract multiple files from a BSA archive in a single parallel pass.
/// Opens the archive once, collects matching entries, then decompresses
/// and writes them in parallel using rayon.
/// `wanted` should contain lowercase backslash-separated paths.
pub fn extract_files_batch<F>(
    bsa_path: &Path,
    wanted: &HashSet<String>,
    callback: F,
) -> Result<usize>
where
    F: Fn(&str, Vec<u8>) -> Result<()> + Send + Sync,
{
    let (archive, options): (Archive, _) = Archive::read(bsa_path)
        .with_context(|| format!("Failed to open BSA: {}", bsa_path.display()))?;

    let compression_options: FileCompressionOptions = (&options).into();

    // Collect matching entries with references to file data
    let mut entries: Vec<(String, &BsaFile)> = Vec::new();
    for (dir_key, folder) in archive.iter() {
        let dir_name = String::from_utf8_lossy(dir_key.name().as_bytes());

        for (file_key, file) in folder.iter() {
            let file_name = String::from_utf8_lossy(file_key.name().as_bytes());

            let full_path = if dir_name.is_empty() || dir_name == "." {
                file_name.to_string()
            } else {
                format!("{}\\{}", dir_name, file_name)
            };

            let lookup = full_path.to_lowercase();
            if wanted.contains(&lookup) {
                entries.push((full_path, file));
            }
        }
    }

    // Decompress + write in parallel
    let extracted = AtomicUsize::new(0);
    entries
        .par_iter()
        .try_for_each(|(path, file)| -> Result<()> {
            let data = if file.is_decompressed() {
                file.as_bytes().to_vec()
            } else {
                file.decompress(&compression_options)?.as_bytes().to_vec()
            };

            callback(path, data)?;
            extracted.fetch_add(1, Ordering::Relaxed);
            Ok(())
        })?;

    let count = extracted.load(Ordering::Relaxed);
    debug!(
        "Batch extracted {} of {} wanted files from BSA {}",
        count,
        wanted.len(),
        bsa_path.display()
    );
    Ok(count)
}

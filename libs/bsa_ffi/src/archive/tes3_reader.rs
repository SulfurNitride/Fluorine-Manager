//! TES3 (Morrowind) BSA reading

use anyhow::{bail, Context, Result};
use ba2::tes3::{Archive, File as Tes3File};
use ba2::{ByteSlice, Reader};
use rayon::prelude::*;
use std::collections::HashSet;
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use tracing::debug;

use super::BsaFileEntry;

/// List all files in a TES3 (Morrowind) BSA archive
pub fn list_files(bsa_path: &Path) -> Result<Vec<BsaFileEntry>> {
    let archive: Archive = Archive::read(bsa_path)
        .with_context(|| format!("Failed to open TES3 BSA: {}", bsa_path.display()))?;

    let mut files = Vec::new();

    for (key, _file) in archive.iter() {
        let path = String::from_utf8_lossy(key.name().as_bytes()).to_string();

        files.push(BsaFileEntry { path });
    }

    debug!(
        "Listed {} files in TES3 BSA {}",
        files.len(),
        bsa_path.display()
    );
    Ok(files)
}

/// Extract a single file from a TES3 (Morrowind) BSA archive
#[allow(dead_code)]
pub fn extract_file(bsa_path: &Path, file_path: &str) -> Result<Vec<u8>> {
    let archive: Archive = Archive::read(bsa_path)
        .with_context(|| format!("Failed to open TES3 BSA: {}", bsa_path.display()))?;

    // Normalize path separators
    let normalized = file_path.replace('/', "\\");

    // Search case-insensitively
    for (key, file) in archive.iter() {
        let current_path = String::from_utf8_lossy(key.name().as_bytes());

        if current_path.eq_ignore_ascii_case(&normalized) {
            // TES3 BSAs are uncompressed, so just return the raw bytes
            return Ok(file.as_bytes().to_vec());
        }
    }

    bail!(
        "File not found in TES3 BSA: {} (looking for '{}')",
        bsa_path.display(),
        file_path
    )
}

/// Extract multiple files from a TES3 BSA archive in parallel.
/// Opens the archive once, collects matching entries, then writes
/// them in parallel using rayon.
/// `wanted` should contain lowercase backslash-separated paths.
pub fn extract_files_batch<F>(
    bsa_path: &Path,
    wanted: &HashSet<String>,
    callback: F,
) -> Result<usize>
where
    F: Fn(&str, Vec<u8>) -> Result<()> + Send + Sync,
{
    let archive: Archive = Archive::read(bsa_path)
        .with_context(|| format!("Failed to open TES3 BSA: {}", bsa_path.display()))?;

    // Collect matching entries
    let mut entries: Vec<(String, &Tes3File)> = Vec::new();
    for (key, file) in archive.iter() {
        let path = String::from_utf8_lossy(key.name().as_bytes()).to_string();
        let lookup = path.to_lowercase();
        if wanted.contains(&lookup) {
            entries.push((path, file));
        }
    }

    // Write in parallel
    let extracted = AtomicUsize::new(0);
    entries
        .par_iter()
        .try_for_each(|(path, file)| -> Result<()> {
            callback(path, file.as_bytes().to_vec())?;
            extracted.fetch_add(1, Ordering::Relaxed);
            Ok(())
        })?;

    let count = extracted.load(Ordering::Relaxed);
    debug!(
        "Batch extracted {} of {} wanted files from TES3 BSA {}",
        count,
        wanted.len(),
        bsa_path.display()
    );
    Ok(count)
}

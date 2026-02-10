//! BA2 (Fallout 4/Starfield) archive reading
//!
//! Provides read support for FO4 format BA2 files (Fallout 4, Fallout 76, Starfield).

use anyhow::{bail, Context, Result};
use ba2::fo4::{Archive, File as Ba2File, FileWriteOptions};
use ba2::prelude::*;
use ba2::ByteSlice;
use rayon::prelude::*;
use std::collections::HashSet;
use std::io::Cursor;
use std::path::Path;
use std::sync::atomic::{AtomicUsize, Ordering};
use tracing::debug;

/// Entry for a file in a BA2 archive
#[derive(Debug, Clone)]
pub struct Ba2FileEntry {
    pub path: String,
}

/// List all files in a BA2 archive
pub fn list_files(ba2_path: &Path) -> Result<Vec<Ba2FileEntry>> {
    let (archive, _options): (Archive, _) = Archive::read(ba2_path)
        .with_context(|| format!("Failed to open BA2: {}", ba2_path.display()))?;

    let mut files = Vec::new();

    for (key, _file) in archive.iter() {
        let path = String::from_utf8_lossy(key.name().as_bytes()).to_string();

        files.push(Ba2FileEntry { path });
    }

    debug!("Listed {} files in BA2 {}", files.len(), ba2_path.display());
    Ok(files)
}

/// Extract a single file from a BA2 archive
#[allow(dead_code)]
pub fn extract_file(ba2_path: &Path, file_path: &str) -> Result<Vec<u8>> {
    let (archive, options): (Archive, _) = Archive::read(ba2_path)
        .with_context(|| format!("Failed to open BA2: {}", ba2_path.display()))?;

    let write_options: FileWriteOptions = options.into();

    // Normalize path for comparison (BA2 uses forward slashes typically)
    let normalized = file_path.replace('\\', "/").to_lowercase();
    let normalized_backslash = file_path.replace('/', "\\").to_lowercase();

    for (key, file) in archive.iter() {
        let current_path = String::from_utf8_lossy(key.name().as_bytes()).to_lowercase();

        // Try both slash conventions
        if current_path == normalized
            || current_path == normalized_backslash
            || current_path.replace('\\', "/") == normalized
            || current_path.replace('/', "\\") == normalized_backslash
        {
            // Write to memory buffer
            let mut buffer = Cursor::new(Vec::new());
            file.write(&mut buffer, &write_options)
                .with_context(|| format!("Failed to extract file: {}", file_path))?;

            return Ok(buffer.into_inner());
        }
    }

    bail!(
        "File not found in BA2: {} (searched for '{}')",
        file_path,
        normalized
    )
}

/// Extract multiple files from a BA2 archive in parallel.
/// Opens the archive once, collects matching entries, then decompresses
/// and writes them in parallel using rayon.
/// `wanted` should contain lowercase forward-slash-separated paths.
pub fn extract_files_batch<F>(
    ba2_path: &Path,
    wanted: &HashSet<String>,
    callback: F,
) -> Result<usize>
where
    F: Fn(&str, Vec<u8>) -> Result<()> + Send + Sync,
{
    let (archive, options): (Archive, _) = Archive::read(ba2_path)
        .with_context(|| format!("Failed to open BA2: {}", ba2_path.display()))?;

    let write_options: FileWriteOptions = options.into();

    // Collect matching entries with references
    let mut entries: Vec<(String, &Ba2File)> = Vec::new();
    for (key, file) in archive.iter() {
        let path = String::from_utf8_lossy(key.name().as_bytes()).to_string();
        let lookup = path.replace('\\', "/").to_lowercase();
        if wanted.contains(&lookup) {
            entries.push((path, file));
        }
    }

    // Decompress + write in parallel
    let extracted = AtomicUsize::new(0);
    entries
        .par_iter()
        .try_for_each(|(path, file)| -> Result<()> {
            let mut buffer = Cursor::new(Vec::new());
            file.write(&mut buffer, &write_options)
                .with_context(|| format!("Failed to extract file: {}", path))?;

            callback(path, buffer.into_inner())?;
            extracted.fetch_add(1, Ordering::Relaxed);
            Ok(())
        })?;

    let count = extracted.load(Ordering::Relaxed);
    debug!(
        "Batch extracted {} of {} wanted files from BA2 {}",
        count,
        wanted.len(),
        ba2_path.display()
    );
    Ok(count)
}

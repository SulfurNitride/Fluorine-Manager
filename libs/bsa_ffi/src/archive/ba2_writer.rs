//! BA2 (Fallout 4/Starfield) archive creation
//!
//! Provides write support for FO4 format BA2 files (Fallout 4, Fallout 76, Starfield).

use anyhow::{bail, Context, Result};
use ba2::fo4::{
    Archive, ArchiveKey, ArchiveOptionsBuilder, Chunk, ChunkCompressionOptions,
    CompressionFormat as Ba2CrateCompression, CompressionLevel, File as Ba2File,
    FileReadOptionsBuilder, Format, Version,
};
use ba2::prelude::*;
use ba2::{CompressionResult, Copied};
use rayon::prelude::*;
use std::collections::HashMap;
use std::fs;
use std::io::BufWriter;
use std::path::Path;
use tracing::info;

/// BA2 archive version
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Ba2Version {
    /// Fallout 4 Old Gen, Fallout 76
    V1,
    /// Starfield
    V2,
    /// Starfield
    V3,
    /// Fallout 4 Next Gen
    #[default]
    V7,
    /// Fallout 4 Next Gen
    V8,
}

impl Ba2Version {
    /// Convert to the ba2 crate's Version type
    pub fn to_crate_version(self) -> Version {
        match self {
            Ba2Version::V1 => Version::v1,
            Ba2Version::V2 => Version::v2,
            Ba2Version::V3 => Version::v3,
            Ba2Version::V7 => Version::v7,
            Ba2Version::V8 => Version::v8,
        }
    }
}

/// Compression format for BA2 archives
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Ba2CompressionFormat {
    /// No compression
    None,
    /// zlib compression (Fallout 4, Fallout 76)
    #[default]
    Zlib,
    /// LZ4 compression (Starfield)
    Lz4,
}

/// Archive format variant
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Ba2Format {
    /// General archive (GNRL) - for meshes, scripts, etc.
    #[default]
    General,
    /// DirectX 10 textures (DX10) - for DDS textures
    DX10,
}

/// Builder for creating BA2 archives
pub struct Ba2Builder {
    /// Files organized by path -> data
    files: HashMap<String, Vec<u8>>,
    /// Archive format (General or DX10)
    format: Ba2Format,
    /// Compression format
    compression: Ba2CompressionFormat,
    /// Whether to include string table
    strings: bool,
    /// Archive version
    version: Ba2Version,
}

impl Ba2Builder {
    pub fn new() -> Self {
        Self {
            files: HashMap::new(),
            format: Ba2Format::General,
            compression: Ba2CompressionFormat::Zlib,
            strings: true,
            version: Ba2Version::default(),
        }
    }

    /// Create builder with settings detected from BA2 name
    #[allow(dead_code)]
    pub fn from_name(name: &str) -> Self {
        let name_lower = name.to_lowercase();

        // Texture archives need DX10 format for proper texture headers
        let is_texture_archive = {
            let filename = name_lower.rsplit(['/', '\\']).next().unwrap_or(&name_lower);
            filename.contains(" - textures")
                || filename.starts_with("textures")
                || (filename.contains("textures")
                    && !filename.contains(" - main")
                    && !filename.contains("_main"))
        };

        let format = if is_texture_archive {
            Ba2Format::DX10
        } else {
            Ba2Format::General
        };

        // Default to zlib compression for FO4
        let compression = Ba2CompressionFormat::Zlib;

        Self {
            files: HashMap::new(),
            format,
            compression,
            strings: true,
            version: Ba2Version::default(),
        }
    }

    /// Set archive version
    pub fn with_version(mut self, version: Ba2Version) -> Self {
        self.version = version;
        self
    }

    /// Set archive format
    pub fn with_format(mut self, format: Ba2Format) -> Self {
        self.format = format;
        self
    }

    /// Set compression format
    pub fn with_compression(mut self, compression: Ba2CompressionFormat) -> Self {
        self.compression = compression;
        self
    }

    /// Enable or disable string table
    #[allow(dead_code)]
    pub fn with_strings(mut self, strings: bool) -> Self {
        self.strings = strings;
        self
    }

    /// Add a file to the archive
    pub fn add_file(&mut self, path: &str, data: Vec<u8>) {
        // Normalize: forward slashes, strip leading slash
        let normalized = path.replace('\\', "/");
        let normalized = normalized.trim_start_matches('/').to_string();
        self.files.insert(normalized, data);
    }

    /// Get number of files
    pub fn file_count(&self) -> usize {
        self.files.len()
    }

    /// Check if empty
    pub fn is_empty(&self) -> bool {
        self.files.is_empty()
    }

    /// Build and write the BA2 to disk with progress callback
    pub fn build_with_progress<F>(self, output_path: &Path, progress: F) -> Result<()>
    where
        F: Fn(usize, usize, &str) + Send + Sync,
    {
        if self.is_empty() {
            bail!("Cannot create empty BA2 archive");
        }

        let file_count = self.file_count();
        let total_size: u64 = self.files.values().map(|data| data.len() as u64).sum();

        info!(
            "Building BA2: {} ({} files, {} MB, format {:?}, compression {:?})",
            output_path.display(),
            file_count,
            total_size / 1_000_000,
            self.format,
            self.compression
        );

        // For DX10 (texture) archives, we need special handling
        if self.format == Ba2Format::DX10 {
            return self.build_dx10_with_progress(output_path, progress);
        }

        // Build archive entries in parallel
        let entries: Vec<(String, Vec<u8>)> = self.files.into_iter().collect();
        let total = entries.len();
        let processed_count = std::sync::atomic::AtomicUsize::new(0);
        let compression = self.compression;

        let archive_entries: Result<Vec<(ArchiveKey<'static>, Ba2File<'static>)>> = entries
            .par_iter()
            .map(|(path, data)| {
                // Create chunk from data
                let chunk = Chunk::from_decompressed(data.clone().into_boxed_slice());

                // Optionally compress the chunk
                let chunk = if compression != Ba2CompressionFormat::None {
                    let options = ChunkCompressionOptions::default();
                    match chunk.compress(&options) {
                        Ok(compressed) => compressed,
                        Err(_) => chunk, // Fall back to uncompressed if compression fails
                    }
                } else {
                    chunk
                };

                // Create file from chunk
                let file: Ba2File = [chunk].into_iter().collect();

                // Create key from path
                let key: ArchiveKey = path.as_bytes().into();

                let current =
                    processed_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
                progress(current, total, path);

                Ok((key, file))
            })
            .collect();

        let archive_entries = archive_entries?;

        // Build archive from entries
        let archive: Archive = archive_entries.into_iter().collect();

        let options = ArchiveOptionsBuilder::default()
            .version(self.version.to_crate_version())
            .strings(self.strings)
            .build();

        // Create parent directory
        if let Some(parent) = output_path.parent() {
            fs::create_dir_all(parent)?;
        }

        // Write archive
        let file = fs::File::create(output_path)
            .with_context(|| format!("Failed to create BA2: {}", output_path.display()))?;
        let mut writer = BufWriter::new(file);

        archive
            .write(&mut writer, &options)
            .with_context(|| format!("Failed to write BA2: {}", output_path.display()))?;

        info!("Created BA2: {}", output_path.display());
        Ok(())
    }

    /// Build a DX10 (texture) archive with progress callback
    fn build_dx10_with_progress<F>(self, output_path: &Path, progress: F) -> Result<()>
    where
        F: Fn(usize, usize, &str) + Send + Sync,
    {
        let compress = self.compression != Ba2CompressionFormat::None;
        let entries: Vec<(String, Vec<u8>)> = self.files.into_iter().collect();
        let total = entries.len();
        let processed_count = std::sync::atomic::AtomicUsize::new(0);

        // Build read options for DX10 format
        let read_options = FileReadOptionsBuilder::new()
            .format(Format::DX10)
            .compression_format(Ba2CrateCompression::Zip)
            .compression_level(CompressionLevel::FO4)
            .compression_result(if compress {
                CompressionResult::Compressed
            } else {
                CompressionResult::Decompressed
            })
            .build();

        let archive_entries: Result<Vec<(ArchiveKey<'static>, Ba2File<'static>)>> = entries
            .par_iter()
            .map(|(path, data)| {
                let file = Ba2File::read(Copied(data), &read_options)
                    .with_context(|| format!("Failed to parse DDS texture: {}", path))?;

                let key: ArchiveKey = path.as_bytes().into();

                let current =
                    processed_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
                progress(current, total, path);

                Ok((key, file))
            })
            .collect();

        let archive_entries = archive_entries?;
        let archive: Archive = archive_entries.into_iter().collect();

        let options = ArchiveOptionsBuilder::default()
            .version(self.version.to_crate_version())
            .format(Format::DX10)
            .compression_format(Ba2CrateCompression::Zip)
            .strings(self.strings)
            .build();

        if let Some(parent) = output_path.parent() {
            fs::create_dir_all(parent)?;
        }

        let file = fs::File::create(output_path)
            .with_context(|| format!("Failed to create BA2: {}", output_path.display()))?;
        let mut writer = BufWriter::new(file);

        archive
            .write(&mut writer, &options)
            .with_context(|| format!("Failed to write BA2: {}", output_path.display()))?;

        info!(
            "Created DX10 BA2: {} ({} files)",
            output_path.display(),
            total
        );
        Ok(())
    }
}

impl Default for Ba2Builder {
    fn default() -> Self {
        Self::new()
    }
}

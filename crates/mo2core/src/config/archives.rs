//! Parser/writer for `archives.txt` - BSA/BA2 archive load order.
//!
//! Format:
//! - One archive filename per line
//! - Lines starting with `#` are comments
//! - Line endings: `\r\n`

use std::path::Path;

/// Parsed archives.txt
#[derive(Debug, Clone, Default)]
pub struct ArchiveList {
    pub archives: Vec<String>,
}

impl ArchiveList {
    /// Parse from string content.
    pub fn parse(content: &str) -> Self {
        let archives = content
            .lines()
            .map(|l| l.trim_end_matches('\r').trim().to_string())
            .filter(|l| !l.is_empty() && !l.starts_with('#'))
            .collect();
        ArchiveList { archives }
    }

    /// Read from file.
    pub fn read(path: &Path) -> anyhow::Result<Self> {
        let content = std::fs::read_to_string(path)?;
        Ok(Self::parse(&content))
    }

    /// Write to string.
    pub fn write_to_string(&self) -> String {
        let mut out = String::new();
        for archive in &self.archives {
            out.push_str(archive);
            out.push_str("\r\n");
        }
        out
    }

    /// Write to file.
    pub fn write(&self, path: &Path) -> anyhow::Result<()> {
        std::fs::write(path, self.write_to_string())?;
        Ok(())
    }

    /// Check if an archive is in the list (case-insensitive).
    pub fn contains(&self, name: &str) -> bool {
        let lower = name.to_lowercase();
        self.archives.iter().any(|a| a.to_lowercase() == lower)
    }

    /// Add an archive if not already present.
    pub fn add(&mut self, name: &str) {
        if !self.contains(name) {
            self.archives.push(name.to_string());
        }
    }

    /// Remove an archive by name (case-insensitive).
    pub fn remove(&mut self, name: &str) -> bool {
        let lower = name.to_lowercase();
        let len = self.archives.len();
        self.archives.retain(|a| a.to_lowercase() != lower);
        self.archives.len() != len
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse() {
        let content = "Skyrim - Textures0.bsa\r\nSkyrim - Textures1.bsa\r\nMyMod.bsa\r\n";
        let list = ArchiveList::parse(content);
        assert_eq!(list.archives.len(), 3);
        assert!(list.contains("skyrim - textures0.bsa"));
    }

    #[test]
    fn test_roundtrip() {
        let content = "Skyrim - Textures0.bsa\r\nMyMod.bsa\r\n";
        let list = ArchiveList::parse(content);
        let output = list.write_to_string();
        assert_eq!(output, content);
    }

    #[test]
    fn test_add_remove() {
        let mut list = ArchiveList::default();
        list.add("Test.bsa");
        assert!(list.contains("test.bsa"));
        assert!(list.remove("TEST.BSA"));
        assert!(!list.contains("test.bsa"));
    }
}

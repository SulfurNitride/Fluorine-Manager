//! Generic INI file parser/writer compatible with QSettings format.
//!
//! QSettings uses a slightly non-standard INI format:
//! - Sections in `[brackets]`
//! - Keys with `=` separator
//! - Values may be quoted or unquoted
//! - `\` escaping in values (backslash doubled)
//! - `@` prefix for special types: `@ByteArray(...)`, `@Size(w h)`, etc.
//! - Comments start with `;` (not `#`)
//! - Nested sections use `/` in key names

use std::collections::BTreeMap;
use std::path::Path;

/// A parsed INI file preserving section order and comments.
#[derive(Debug, Clone, Default)]
pub struct IniFile {
    /// Sections in order. Empty string key = global (no section header).
    pub sections: Vec<IniSection>,
}

#[derive(Debug, Clone)]
pub struct IniSection {
    pub name: String,
    pub entries: Vec<IniEntry>,
}

#[derive(Debug, Clone)]
pub enum IniEntry {
    Comment(String),
    KeyValue { key: String, value: String },
    Blank,
}

impl IniFile {
    /// Parse an INI file from string content.
    pub fn parse(content: &str) -> Self {
        let mut sections = Vec::new();
        let mut current_section = IniSection {
            name: String::new(),
            entries: Vec::new(),
        };

        for line in content.lines() {
            let trimmed = line.trim();

            if trimmed.is_empty() {
                current_section.entries.push(IniEntry::Blank);
            } else if trimmed.starts_with(';') || trimmed.starts_with('#') {
                current_section
                    .entries
                    .push(IniEntry::Comment(trimmed.to_string()));
            } else if trimmed.starts_with('[') && trimmed.ends_with(']') {
                // New section
                sections.push(current_section);
                current_section = IniSection {
                    name: trimmed[1..trimmed.len() - 1].to_string(),
                    entries: Vec::new(),
                };
            } else if let Some(eq_pos) = trimmed.find('=') {
                let key = trimmed[..eq_pos].trim().to_string();
                let value = trimmed[eq_pos + 1..].trim().to_string();
                current_section
                    .entries
                    .push(IniEntry::KeyValue { key, value });
            } else {
                // Treat unknown lines as comments
                current_section
                    .entries
                    .push(IniEntry::Comment(trimmed.to_string()));
            }
        }

        sections.push(current_section);
        IniFile { sections }
    }

    /// Read and parse an INI file from disk.
    pub fn read(path: &Path) -> anyhow::Result<Self> {
        let content = std::fs::read_to_string(path)?;
        Ok(Self::parse(&content))
    }

    /// Write the INI file with CRLF line endings (MO2 format).
    pub fn write_to_string(&self) -> String {
        let mut out = String::new();
        let mut first_named = true;
        for section in &self.sections {
            if !section.name.is_empty() {
                // Add blank line between sections, but not before the first one
                // unless the global section had content
                if !first_named {
                    out.push_str("\r\n");
                }
                first_named = false;
                out.push('[');
                out.push_str(&section.name);
                out.push_str("]\r\n");
            } else if section.entries.is_empty() {
                // Skip empty global section entirely
                continue;
            }
            for entry in &section.entries {
                match entry {
                    IniEntry::Comment(c) => {
                        out.push_str(c);
                        out.push_str("\r\n");
                    }
                    IniEntry::KeyValue { key, value } => {
                        out.push_str(key);
                        out.push('=');
                        out.push_str(value);
                        out.push_str("\r\n");
                    }
                    IniEntry::Blank => {
                        out.push_str("\r\n");
                    }
                }
            }
        }
        out
    }

    /// Write the INI file to disk.
    pub fn write(&self, path: &Path) -> anyhow::Result<()> {
        let content = self.write_to_string();
        std::fs::write(path, content)?;
        Ok(())
    }

    /// Get a value from a specific section by key.
    pub fn get(&self, section: &str, key: &str) -> Option<&str> {
        self.find_section(section).and_then(|s| {
            s.entries.iter().find_map(|e| match e {
                IniEntry::KeyValue { key: k, value } if k == key => Some(value.as_str()),
                _ => None,
            })
        })
    }

    /// Set a value in a specific section. Creates section/key if needed.
    pub fn set(&mut self, section: &str, key: &str, value: &str) {
        let sec = self.find_or_create_section(section);
        for entry in sec.entries.iter_mut() {
            if let IniEntry::KeyValue { key: k, value: v } = entry {
                if k == key {
                    *v = value.to_string();
                    return;
                }
            }
        }
        sec.entries.push(IniEntry::KeyValue {
            key: key.to_string(),
            value: value.to_string(),
        });
    }

    /// Remove a key from a section.
    pub fn remove(&mut self, section: &str, key: &str) -> bool {
        if let Some(sec) = self.find_section_mut(section) {
            let len_before = sec.entries.len();
            sec.entries.retain(|e| match e {
                IniEntry::KeyValue { key: k, .. } => k != key,
                _ => true,
            });
            sec.entries.len() != len_before
        } else {
            false
        }
    }

    /// Get all key-value pairs in a section as a BTreeMap.
    pub fn section_map(&self, section: &str) -> BTreeMap<String, String> {
        let mut map = BTreeMap::new();
        if let Some(sec) = self.find_section(section) {
            for entry in &sec.entries {
                if let IniEntry::KeyValue { key, value } = entry {
                    map.insert(key.clone(), value.clone());
                }
            }
        }
        map
    }

    /// Get all keys matching a prefix pattern in a section.
    /// E.g., `keys_with_prefix("installedFiles", "1\\")` returns all sub-keys.
    pub fn keys_with_prefix(&self, section: &str, prefix: &str) -> Vec<(String, String)> {
        let mut results = Vec::new();
        if let Some(sec) = self.find_section(section) {
            for entry in &sec.entries {
                if let IniEntry::KeyValue { key, value } = entry {
                    if key.starts_with(prefix) {
                        results.push((key.clone(), value.clone()));
                    }
                }
            }
        }
        results
    }

    fn find_section(&self, name: &str) -> Option<&IniSection> {
        self.sections.iter().find(|s| s.name == name)
    }

    fn find_section_mut(&mut self, name: &str) -> Option<&mut IniSection> {
        self.sections.iter_mut().find(|s| s.name == name)
    }

    fn find_or_create_section(&mut self, name: &str) -> &mut IniSection {
        if !self.sections.iter().any(|s| s.name == name) {
            self.sections.push(IniSection {
                name: name.to_string(),
                entries: Vec::new(),
            });
        }
        self.sections.iter_mut().find(|s| s.name == name).unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_basic() {
        let content = "[General]\r\ngameName=Skyrim Special Edition\r\nselectedProfile=Default\r\n";
        let ini = IniFile::parse(content);
        assert_eq!(
            ini.get("General", "gameName"),
            Some("Skyrim Special Edition")
        );
        assert_eq!(ini.get("General", "selectedProfile"), Some("Default"));
    }

    #[test]
    fn test_roundtrip() {
        let content = "[General]\r\ngameName=Skyrim Special Edition\r\nselectedProfile=Default\r\n";
        let ini = IniFile::parse(content);
        let output = ini.write_to_string();
        assert_eq!(output, content);
    }

    #[test]
    fn test_set_and_get() {
        let mut ini = IniFile::default();
        ini.set("General", "gameName", "Skyrim");
        ini.set("General", "version", "2.5.0");
        assert_eq!(ini.get("General", "gameName"), Some("Skyrim"));
        assert_eq!(ini.get("General", "version"), Some("2.5.0"));
    }

    #[test]
    fn test_remove() {
        let mut ini = IniFile::default();
        ini.set("General", "gameName", "Skyrim");
        ini.set("General", "version", "2.5.0");
        assert!(ini.remove("General", "version"));
        assert_eq!(ini.get("General", "version"), None);
        assert!(!ini.remove("General", "nonexistent"));
    }

    #[test]
    fn test_section_map() {
        let content = "[General]\r\nkey1=val1\r\nkey2=val2\r\n[Other]\r\nkey3=val3\r\n";
        let ini = IniFile::parse(content);
        let map = ini.section_map("General");
        assert_eq!(map.len(), 2);
        assert_eq!(map["key1"], "val1");
        assert_eq!(map["key2"], "val2");
    }

    #[test]
    fn test_comments_preserved() {
        let content = "; comment line\r\n[Section]\r\nkey=val\r\n";
        let ini = IniFile::parse(content);
        let output = ini.write_to_string();
        assert!(output.contains("; comment line"));
    }
}

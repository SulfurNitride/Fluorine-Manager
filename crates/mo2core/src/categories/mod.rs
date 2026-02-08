//! Category management for organizing mods.
//!
//! MO2 uses a hierarchical category system where each category has:
//! - An ID number
//! - A name
//! - An optional parent category ID
//! - An optional Nexus category ID mapping

use std::collections::HashMap;
use std::path::Path;

/// A single category definition.
#[derive(Debug, Clone)]
pub struct Category {
    pub id: i32,
    pub name: String,
    pub parent_id: Option<i32>,
    pub nexus_ids: Vec<i32>,
}

/// Category tree manager.
#[derive(Debug, Clone, Default)]
pub struct Categories {
    pub categories: Vec<Category>,
    index: HashMap<i32, usize>,
}

impl Categories {
    /// Load categories from a categories.dat file.
    ///
    /// Format: pipe-delimited lines:
    /// `id|parent_id|nexus_id1,nexus_id2|name`
    pub fn parse(content: &str) -> Self {
        let mut categories = Vec::new();
        let mut index = HashMap::new();

        for line in content.lines() {
            let line = line.trim_end_matches('\r').trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }

            let parts: Vec<&str> = line.splitn(4, '|').collect();
            if parts.len() < 4 {
                continue;
            }

            let id: i32 = match parts[0].parse() {
                Ok(id) => id,
                Err(_) => continue,
            };

            let parent_id = parts[1].parse::<i32>().ok().filter(|&p| p > 0);

            let nexus_ids: Vec<i32> = parts[2]
                .split(',')
                .filter_map(|s| s.trim().parse().ok())
                .collect();

            let name = parts[3].to_string();

            let idx = categories.len();
            categories.push(Category {
                id,
                name,
                parent_id,
                nexus_ids,
            });
            index.insert(id, idx);
        }

        Categories { categories, index }
    }

    /// Read from file.
    pub fn read(path: &Path) -> anyhow::Result<Self> {
        let content = std::fs::read_to_string(path)?;
        Ok(Self::parse(&content))
    }

    /// Write to string.
    pub fn write_to_string(&self) -> String {
        let mut out = String::new();
        for cat in &self.categories {
            out.push_str(&cat.id.to_string());
            out.push('|');
            out.push_str(&cat.parent_id.map_or("0".to_string(), |p| p.to_string()));
            out.push('|');
            let nexus_str: String = cat
                .nexus_ids
                .iter()
                .map(|n| n.to_string())
                .collect::<Vec<_>>()
                .join(",");
            out.push_str(&nexus_str);
            out.push('|');
            out.push_str(&cat.name);
            out.push_str("\r\n");
        }
        out
    }

    /// Write to file.
    pub fn write(&self, path: &Path) -> anyhow::Result<()> {
        std::fs::write(path, self.write_to_string())?;
        Ok(())
    }

    /// Get a category by ID.
    pub fn get(&self, id: i32) -> Option<&Category> {
        self.index.get(&id).map(|&idx| &self.categories[idx])
    }

    /// Get the name of a category.
    pub fn name(&self, id: i32) -> Option<&str> {
        self.get(id).map(|c| c.name.as_str())
    }

    /// Get child categories of a parent.
    pub fn children(&self, parent_id: i32) -> Vec<&Category> {
        self.categories
            .iter()
            .filter(|c| c.parent_id == Some(parent_id))
            .collect()
    }

    /// Get top-level categories (no parent).
    pub fn top_level(&self) -> Vec<&Category> {
        self.categories
            .iter()
            .filter(|c| c.parent_id.is_none())
            .collect()
    }

    /// Find a category by Nexus category ID.
    pub fn find_by_nexus_id(&self, nexus_id: i32) -> Option<&Category> {
        self.categories
            .iter()
            .find(|c| c.nexus_ids.contains(&nexus_id))
    }

    /// Get the full path of category names (e.g., "Gameplay > Combat").
    pub fn full_path(&self, id: i32) -> String {
        let mut parts = Vec::new();
        let mut current_id = Some(id);

        while let Some(cid) = current_id {
            if let Some(cat) = self.get(cid) {
                parts.push(cat.name.as_str());
                current_id = cat.parent_id;
            } else {
                break;
            }
        }

        parts.reverse();
        parts.join(" > ")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE_CATS: &str = "\
1|0||Animations\r\n\
2|0||Gameplay\r\n\
3|2||Combat\r\n\
4|2|54|Magic\r\n\
5|0|7,8|Textures\r\n";

    #[test]
    fn test_parse() {
        let cats = Categories::parse(SAMPLE_CATS);
        assert_eq!(cats.categories.len(), 5);
        assert_eq!(cats.get(1).unwrap().name, "Animations");
        assert_eq!(cats.get(3).unwrap().parent_id, Some(2));
    }

    #[test]
    fn test_hierarchy() {
        let cats = Categories::parse(SAMPLE_CATS);
        let top = cats.top_level();
        assert_eq!(top.len(), 3); // Animations, Gameplay, Textures

        let children = cats.children(2);
        assert_eq!(children.len(), 2); // Combat, Magic
    }

    #[test]
    fn test_full_path() {
        let cats = Categories::parse(SAMPLE_CATS);
        assert_eq!(cats.full_path(3), "Gameplay > Combat");
        assert_eq!(cats.full_path(1), "Animations");
    }

    #[test]
    fn test_nexus_lookup() {
        let cats = Categories::parse(SAMPLE_CATS);
        let found = cats.find_by_nexus_id(54).unwrap();
        assert_eq!(found.name, "Magic");

        let found = cats.find_by_nexus_id(7).unwrap();
        assert_eq!(found.name, "Textures");
    }
}

//! Executable definitions from MO2's `[customExecutables]` section.
//!
//! MO2 stores custom executables in QSettings numbered-array format:
//! ```ini
//! [customExecutables]
//! size=2
//! 1\title=SKSE
//! 1\binary=/path/to/skse_loader.exe
//! 1\arguments=-forcesteamloader
//! 1\workingDirectory=/path/to/game
//! 1\toolbar=true
//! 1\hide=false
//! 2\title=xEdit
//! 2\binary=/path/to/xEdit.exe
//! ...
//! ```

use std::path::Path;

use super::ini::IniFile;
use crate::paths::normalize_any_path;

const SECTION: &str = "customExecutables";

/// A single executable/tool entry.
#[derive(Debug, Clone, Default)]
pub struct Executable {
    pub title: String,
    pub binary: String,
    pub arguments: String,
    pub working_directory: String,
    pub steam_app_id: String,
    pub show_in_toolbar: bool,
    pub use_own_icon: bool,
    pub hide: bool,
}

/// Collection of executables read from/written to INI.
#[derive(Debug, Clone, Default)]
pub struct ExecutablesList {
    pub executables: Vec<Executable>,
}

impl ExecutablesList {
    /// Read executables from an INI file's `[customExecutables]` section.
    pub fn read_from_ini(ini: &IniFile) -> Self {
        Self::read_from_ini_with_prefix(ini, None)
    }

    /// Read executables from INI and normalize path fields to Linux-native paths.
    ///
    /// If `wine_prefix` is provided, `C:` paths are resolved into `drive_c`.
    pub fn read_from_ini_with_prefix(ini: &IniFile, wine_prefix: Option<&Path>) -> Self {
        let size: usize = ini
            .get(SECTION, "size")
            .and_then(|s| s.parse().ok())
            .unwrap_or(0);

        let mut executables = Vec::with_capacity(size);
        for i in 1..=size {
            let prefix = format!("{}\\", i);
            let get = |key: &str| -> String {
                ini.get(SECTION, &format!("{}{}", prefix, key))
                    .unwrap_or("")
                    .to_string()
            };
            let get_bool = |key: &str| -> bool {
                ini.get(SECTION, &format!("{}{}", prefix, key))
                    .map(|v| v == "true" || v == "1")
                    .unwrap_or(false)
            };

            let title = get("title");
            if title.is_empty() {
                continue; // Skip entries without a title
            }

            executables.push(Executable {
                title,
                binary: normalize_executable_path(&get("binary"), wine_prefix),
                arguments: get("arguments"),
                working_directory: normalize_executable_path(&get("workingDirectory"), wine_prefix),
                steam_app_id: get("steamAppID"),
                show_in_toolbar: get_bool("toolbar"),
                use_own_icon: get_bool("ownIcon"),
                hide: get_bool("hide"),
            });
        }

        ExecutablesList { executables }
    }

    /// Write executables to an INI file's `[customExecutables]` section.
    /// Clears existing entries and writes fresh.
    pub fn write_to_ini(&self, ini: &mut IniFile) {
        // Remove existing section entries
        if let Some(sec) = ini.sections.iter_mut().find(|s| s.name == SECTION) {
            sec.entries.clear();
        }

        ini.set(SECTION, "size", &self.executables.len().to_string());

        for (i, exe) in self.executables.iter().enumerate() {
            let idx = i + 1;
            let prefix = format!("{}\\", idx);
            ini.set(SECTION, &format!("{}title", prefix), &exe.title);
            ini.set(SECTION, &format!("{}binary", prefix), &exe.binary);
            ini.set(SECTION, &format!("{}arguments", prefix), &exe.arguments);
            ini.set(
                SECTION,
                &format!("{}workingDirectory", prefix),
                &exe.working_directory,
            );
            ini.set(SECTION, &format!("{}steamAppID", prefix), &exe.steam_app_id);
            ini.set(
                SECTION,
                &format!("{}toolbar", prefix),
                if exe.show_in_toolbar { "true" } else { "false" },
            );
            ini.set(
                SECTION,
                &format!("{}ownIcon", prefix),
                if exe.use_own_icon { "true" } else { "false" },
            );
            ini.set(
                SECTION,
                &format!("{}hide", prefix),
                if exe.hide { "true" } else { "false" },
            );
        }
    }

    /// Add an executable.
    pub fn add(&mut self, exe: Executable) {
        self.executables.push(exe);
    }

    /// Remove an executable by index.
    pub fn remove(&mut self, index: usize) -> Option<Executable> {
        if index < self.executables.len() {
            Some(self.executables.remove(index))
        } else {
            None
        }
    }

    /// Find an executable by title (case-insensitive).
    pub fn find(&self, title: &str) -> Option<&Executable> {
        let lower = title.to_lowercase();
        self.executables
            .iter()
            .find(|e| e.title.to_lowercase() == lower)
    }
}

fn normalize_executable_path(path: &str, wine_prefix: Option<&Path>) -> String {
    if path.trim().is_empty() {
        String::new()
    } else {
        normalize_any_path(path, wine_prefix)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_ini() -> IniFile {
        let content = "\
[customExecutables]\r\n\
size=2\r\n\
1\\title=SKSE\r\n\
1\\binary=/home/user/game/skse_loader.exe\r\n\
1\\arguments=-forcesteamloader\r\n\
1\\workingDirectory=/home/user/game\r\n\
1\\toolbar=true\r\n\
1\\hide=false\r\n\
2\\title=xEdit\r\n\
2\\binary=/home/user/tools/xEdit.exe\r\n\
2\\arguments=\r\n\
2\\workingDirectory=\r\n\
2\\toolbar=false\r\n\
2\\hide=false\r\n";
        IniFile::parse(content)
    }

    #[test]
    fn test_read_executables() {
        let ini = sample_ini();
        let list = ExecutablesList::read_from_ini(&ini);
        assert_eq!(list.executables.len(), 2);

        assert_eq!(list.executables[0].title, "SKSE");
        assert_eq!(
            list.executables[0].binary,
            "/home/user/game/skse_loader.exe"
        );
        assert_eq!(list.executables[0].arguments, "-forcesteamloader");
        assert!(list.executables[0].show_in_toolbar);
        assert!(!list.executables[0].hide);

        assert_eq!(list.executables[1].title, "xEdit");
        assert_eq!(list.executables[1].binary, "/home/user/tools/xEdit.exe");
        assert!(!list.executables[1].show_in_toolbar);
    }

    #[test]
    fn test_read_executables_normalizes_wine_z_paths() {
        let ini = IniFile::parse(
            "[customExecutables]\n\
             size=1\n\
             1\\title=SKSE\n\
             1\\binary=Z:/home/user/game/skse_loader.exe\n\
             1\\workingDirectory=Z:\\home\\user\\game\n",
        );
        let list = ExecutablesList::read_from_ini(&ini);
        assert_eq!(list.executables.len(), 1);
        assert_eq!(
            list.executables[0].binary,
            "/home/user/game/skse_loader.exe"
        );
        assert_eq!(list.executables[0].working_directory, "/home/user/game");
    }

    #[test]
    fn test_write_executables() {
        let mut list = ExecutablesList::default();
        list.add(Executable {
            title: "SKSE".to_string(),
            binary: "/path/to/skse".to_string(),
            arguments: "-forcesteamloader".to_string(),
            show_in_toolbar: true,
            ..Default::default()
        });
        list.add(Executable {
            title: "xEdit".to_string(),
            binary: "/path/to/xedit".to_string(),
            ..Default::default()
        });

        let mut ini = IniFile::default();
        list.write_to_ini(&mut ini);

        assert_eq!(ini.get(SECTION, "size"), Some("2"));
        assert_eq!(ini.get(SECTION, "1\\title"), Some("SKSE"));
        assert_eq!(ini.get(SECTION, "1\\binary"), Some("/path/to/skse"));
        assert_eq!(ini.get(SECTION, "1\\toolbar"), Some("true"));
        assert_eq!(ini.get(SECTION, "2\\title"), Some("xEdit"));
        assert_eq!(ini.get(SECTION, "2\\toolbar"), Some("false"));
    }

    #[test]
    fn test_roundtrip() {
        let ini = sample_ini();
        let list = ExecutablesList::read_from_ini(&ini);

        let mut new_ini = IniFile::default();
        list.write_to_ini(&mut new_ini);

        let reloaded = ExecutablesList::read_from_ini(&new_ini);
        assert_eq!(reloaded.executables.len(), 2);
        assert_eq!(reloaded.executables[0].title, "SKSE");
        assert_eq!(reloaded.executables[1].title, "xEdit");
    }

    #[test]
    fn test_add_and_remove() {
        let mut list = ExecutablesList::default();
        list.add(Executable {
            title: "Tool1".to_string(),
            ..Default::default()
        });
        list.add(Executable {
            title: "Tool2".to_string(),
            ..Default::default()
        });
        assert_eq!(list.executables.len(), 2);

        let removed = list.remove(0).unwrap();
        assert_eq!(removed.title, "Tool1");
        assert_eq!(list.executables.len(), 1);
        assert_eq!(list.executables[0].title, "Tool2");
    }

    #[test]
    fn test_find() {
        let ini = sample_ini();
        let list = ExecutablesList::read_from_ini(&ini);

        assert!(list.find("SKSE").is_some());
        assert!(list.find("skse").is_some()); // case-insensitive
        assert!(list.find("NonExistent").is_none());
    }

    #[test]
    fn test_empty_section() {
        let ini = IniFile::parse("");
        let list = ExecutablesList::read_from_ini(&ini);
        assert_eq!(list.executables.len(), 0);
    }
}

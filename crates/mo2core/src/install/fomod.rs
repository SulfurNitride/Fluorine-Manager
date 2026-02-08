//! FOMOD (Fallout Mod) installer XML parser.
//!
//! Parses `fomod/ModuleConfig.xml` and resolves user selections into
//! a list of files to install.
//!
//! Reference: <https://fomod-docs.readthedocs.io/en/latest/specs.html>

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use quick_xml::events::Event;
use quick_xml::Reader;

use super::FileInstallAction;

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

/// A parsed FOMOD ModuleConfig.
#[derive(Debug, Clone)]
pub struct FomodConfig {
    pub module_name: String,
    pub required_files: Vec<FomodFile>,
    pub install_steps: Vec<InstallStep>,
    pub conditional_installs: Vec<ConditionalPattern>,
}

/// An install step (page) shown to the user.
#[derive(Debug, Clone)]
pub struct InstallStep {
    pub name: String,
    /// Visibility condition flags (all must match for step to be visible).
    pub visible_conditions: Vec<FlagCondition>,
    pub groups: Vec<OptionGroup>,
}

/// A group of options within a step.
#[derive(Debug, Clone)]
pub struct OptionGroup {
    pub name: String,
    pub group_type: GroupType,
    pub plugins: Vec<PluginOption>,
}

/// Selection constraint for a group.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum GroupType {
    SelectExactlyOne,
    SelectAny,
    SelectAtLeastOne,
    SelectAtMostOne,
    SelectAll,
}

impl GroupType {
    fn from_str(s: &str) -> Self {
        match s {
            "SelectExactlyOne" => GroupType::SelectExactlyOne,
            "SelectAny" => GroupType::SelectAny,
            "SelectAtLeastOne" => GroupType::SelectAtLeastOne,
            "SelectAtMostOne" => GroupType::SelectAtMostOne,
            "SelectAll" => GroupType::SelectAll,
            _ => GroupType::SelectAny,
        }
    }
}

/// A single option (plugin) within a group.
#[derive(Debug, Clone)]
pub struct PluginOption {
    pub name: String,
    pub description: String,
    pub image_path: Option<String>,
    pub files: Vec<FomodFile>,
    pub condition_flags: Vec<FlagSet>,
    pub type_name: PluginType,
}

/// Plugin type descriptor.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PluginType {
    Required,
    Recommended,
    Optional,
    NotUsable,
    CouldBeUsable,
}

impl PluginType {
    fn from_str(s: &str) -> Self {
        match s {
            "Required" => PluginType::Required,
            "Recommended" => PluginType::Recommended,
            "Optional" => PluginType::Optional,
            "NotUsable" => PluginType::NotUsable,
            "CouldBeUsable" => PluginType::CouldBeUsable,
            _ => PluginType::Optional,
        }
    }
}

/// A file or folder to install.
#[derive(Debug, Clone)]
pub struct FomodFile {
    pub source: String,
    pub destination: String,
    pub is_folder: bool,
    pub priority: i32,
}

/// A condition flag to check.
#[derive(Debug, Clone)]
pub struct FlagCondition {
    pub flag: String,
    pub value: String,
}

/// A flag to set when an option is selected.
#[derive(Debug, Clone)]
pub struct FlagSet {
    pub name: String,
    pub value: String,
}

/// Conditional file installation pattern.
#[derive(Debug, Clone)]
pub struct ConditionalPattern {
    pub conditions: Vec<FlagCondition>,
    pub files: Vec<FomodFile>,
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

/// Parse a FOMOD ModuleConfig.xml file.
pub fn parse_module_config(path: &Path) -> Result<FomodConfig> {
    let bytes = std::fs::read(path).with_context(|| format!("Failed to read {:?}", path))?;
    let content = if bytes.starts_with(&[0xFF, 0xFE]) {
        // UTF-16 LE BOM
        let u16s: Vec<u16> = bytes[2..]
            .chunks_exact(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        String::from_utf16_lossy(&u16s)
    } else if bytes.starts_with(&[0xFE, 0xFF]) {
        // UTF-16 BE BOM
        let u16s: Vec<u16> = bytes[2..]
            .chunks_exact(2)
            .map(|c| u16::from_be_bytes([c[0], c[1]]))
            .collect();
        String::from_utf16_lossy(&u16s)
    } else {
        String::from_utf8_lossy(&bytes).into_owned()
    };

    // Strip XML declaration encoding issues
    let content = content.trim_start_matches('\u{FEFF}');

    parse_module_config_str(content)
}

/// Parse FOMOD XML from a string.
pub fn parse_module_config_str(xml: &str) -> Result<FomodConfig> {
    let mut reader = Reader::from_str(xml);
    reader.config_mut().trim_text(true);

    let mut config = FomodConfig {
        module_name: String::new(),
        required_files: Vec::new(),
        install_steps: Vec::new(),
        conditional_installs: Vec::new(),
    };

    let mut buf = Vec::new();

    loop {
        match reader.read_event_into(&mut buf) {
            Ok(Event::Start(e)) => match e.name().as_ref() {
                b"moduleName" => {
                    config.module_name = read_text(&mut reader, &mut buf)?;
                }
                b"requiredInstallFiles" => {
                    config.required_files =
                        read_file_list(&mut reader, &mut buf, b"requiredInstallFiles")?;
                }
                b"installSteps" => {
                    config.install_steps = read_install_steps(&mut reader, &mut buf)?;
                }
                b"conditionalFileInstalls" => {
                    config.conditional_installs = read_conditional_installs(&mut reader, &mut buf)?;
                }
                _ => {}
            },
            Ok(Event::Eof) => break,
            Err(e) => {
                tracing::warn!("FOMOD XML parse warning: {e}");
                break;
            }
            _ => {}
        }
        buf.clear();
    }

    Ok(config)
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

/// User selections: maps (step_index, group_index) → set of selected plugin indices.
pub type UserSelections = HashMap<(usize, usize), Vec<usize>>;

/// Resolve user selections into a flat list of file install actions.
///
/// Combines:
/// 1. Required files (always installed)
/// 2. Files from selected plugins
/// 3. Conditional file installs (if flag conditions are met)
pub fn resolve_installation(
    config: &FomodConfig,
    selections: &UserSelections,
) -> Vec<FileInstallAction> {
    let mut actions: Vec<FileInstallAction> = Vec::new();
    let mut flags: HashMap<String, String> = HashMap::new();

    // 1. Required files
    for f in &config.required_files {
        actions.push(fomod_file_to_action(f));
    }

    // 2. Selected plugin files + collect flags
    for (step_idx, step) in config.install_steps.iter().enumerate() {
        for (group_idx, group) in step.groups.iter().enumerate() {
            let selected = selections.get(&(step_idx, group_idx));
            let indices: Vec<usize> = match selected {
                Some(indices) => indices.clone(),
                None => {
                    // Auto-select Required and Recommended for unset groups
                    group
                        .plugins
                        .iter()
                        .enumerate()
                        .filter(|(_, p)| {
                            matches!(p.type_name, PluginType::Required)
                                || (group.group_type == GroupType::SelectAll)
                        })
                        .map(|(i, _)| i)
                        .collect()
                }
            };

            for &idx in &indices {
                if let Some(plugin) = group.plugins.get(idx) {
                    for f in &plugin.files {
                        actions.push(fomod_file_to_action(f));
                    }
                    for flag in &plugin.condition_flags {
                        flags.insert(flag.name.clone(), flag.value.clone());
                    }
                }
            }
        }
    }

    // 3. Conditional file installs
    for pattern in &config.conditional_installs {
        let all_match = pattern
            .conditions
            .iter()
            .all(|c| flags.get(&c.flag).map(|v| v == &c.value).unwrap_or(false));
        if all_match {
            for f in &pattern.files {
                actions.push(fomod_file_to_action(f));
            }
        }
    }

    // Sort by priority (lower = installed first, higher overwrites)
    actions.sort_by_key(|_| 0); // all same priority for now
    actions
}

/// Check which install steps are visible given current flag state.
pub fn visible_steps(config: &FomodConfig, flags: &HashMap<String, String>) -> Vec<usize> {
    config
        .install_steps
        .iter()
        .enumerate()
        .filter(|(_, step)| {
            step.visible_conditions.is_empty()
                || step
                    .visible_conditions
                    .iter()
                    .all(|c| flags.get(&c.flag).map(|v| v == &c.value).unwrap_or(false))
        })
        .map(|(i, _)| i)
        .collect()
}

/// Collect all flags set by the current selections.
pub fn collect_flags(config: &FomodConfig, selections: &UserSelections) -> HashMap<String, String> {
    let mut flags = HashMap::new();
    for (step_idx, step) in config.install_steps.iter().enumerate() {
        for (group_idx, group) in step.groups.iter().enumerate() {
            if let Some(indices) = selections.get(&(step_idx, group_idx)) {
                for &idx in indices {
                    if let Some(plugin) = group.plugins.get(idx) {
                        for flag in &plugin.condition_flags {
                            flags.insert(flag.name.clone(), flag.value.clone());
                        }
                    }
                }
            }
        }
    }
    flags
}

fn fomod_file_to_action(f: &FomodFile) -> FileInstallAction {
    FileInstallAction {
        source: PathBuf::from(&f.source),
        destination: if f.destination.is_empty() {
            PathBuf::from(&f.source)
        } else {
            PathBuf::from(&f.destination)
        },
    }
}

// ---------------------------------------------------------------------------
// XML reading helpers
// ---------------------------------------------------------------------------

fn read_text(reader: &mut Reader<&[u8]>, buf: &mut Vec<u8>) -> Result<String> {
    let mut text = String::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Text(e)) => {
                let decoded = e.decode().map_err(|e| anyhow::anyhow!("{e}"))?;
                text.push_str(&decoded);
            }
            Ok(Event::End(_)) | Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(text)
}

fn read_file_list(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
    end_tag: &[u8],
) -> Result<Vec<FomodFile>> {
    let mut files = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Empty(e)) | Ok(Event::Start(e)) => {
                let is_folder = e.name().as_ref() == b"folder";
                if e.name().as_ref() == b"file" || is_folder {
                    let mut source = String::new();
                    let mut destination = String::new();
                    let mut priority = 0;
                    for attr in e.attributes().flatten() {
                        match attr.key.as_ref() {
                            b"source" => source = attr.unescape_value()?.into_owned(),
                            b"destination" => destination = attr.unescape_value()?.into_owned(),
                            b"priority" => priority = attr.unescape_value()?.parse().unwrap_or(0),
                            _ => {}
                        }
                    }
                    files.push(FomodFile {
                        source,
                        destination,
                        is_folder,
                        priority,
                    });
                }
            }
            Ok(Event::End(e)) if e.name().as_ref() == end_tag => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(files)
}

fn read_install_steps(reader: &mut Reader<&[u8]>, buf: &mut Vec<u8>) -> Result<Vec<InstallStep>> {
    let mut steps = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) if e.name().as_ref() == b"installStep" => {
                let name = get_attr(&e, "name").unwrap_or_default();
                steps.push(read_install_step(reader, buf, name)?);
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"installSteps" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(steps)
}

fn read_install_step(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
    name: String,
) -> Result<InstallStep> {
    let mut step = InstallStep {
        name,
        visible_conditions: Vec::new(),
        groups: Vec::new(),
    };

    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) => match e.name().as_ref() {
                b"visible" => {
                    step.visible_conditions = read_flag_conditions(reader, buf, b"visible")?;
                }
                b"optionalFileGroups" => {
                    step.groups = read_option_groups(reader, buf)?;
                }
                _ => {}
            },
            Ok(Event::End(e)) if e.name().as_ref() == b"installStep" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(step)
}

fn read_flag_conditions(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
    end_tag: &[u8],
) -> Result<Vec<FlagCondition>> {
    let mut conditions = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Empty(e)) if e.name().as_ref() == b"flagDependency" => {
                let flag = get_attr(&e, "flag").unwrap_or_default();
                let value = get_attr(&e, "value").unwrap_or_default();
                conditions.push(FlagCondition { flag, value });
            }
            Ok(Event::End(e)) if e.name().as_ref() == end_tag => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(conditions)
}

fn read_option_groups(reader: &mut Reader<&[u8]>, buf: &mut Vec<u8>) -> Result<Vec<OptionGroup>> {
    let mut groups = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) if e.name().as_ref() == b"group" => {
                let name = get_attr(&e, "name").unwrap_or_default();
                let group_type = get_attr(&e, "type")
                    .map(|t| GroupType::from_str(&t))
                    .unwrap_or(GroupType::SelectAny);
                groups.push(read_option_group(reader, buf, name, group_type)?);
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"optionalFileGroups" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(groups)
}

fn read_option_group(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
    name: String,
    group_type: GroupType,
) -> Result<OptionGroup> {
    let mut plugins = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) if e.name().as_ref() == b"plugin" => {
                let pname = get_attr(&e, "name").unwrap_or_default();
                plugins.push(read_plugin(reader, buf, pname)?);
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"group" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(OptionGroup {
        name,
        group_type,
        plugins,
    })
}

fn read_plugin(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
    name: String,
) -> Result<PluginOption> {
    let mut plugin = PluginOption {
        name,
        description: String::new(),
        image_path: None,
        files: Vec::new(),
        condition_flags: Vec::new(),
        type_name: PluginType::Optional,
    };

    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) => match e.name().as_ref() {
                b"description" => {
                    plugin.description = read_text(reader, buf)?;
                }
                b"files" => {
                    plugin.files = read_file_list(reader, buf, b"files")?;
                }
                b"conditionFlags" => {
                    plugin.condition_flags = read_flag_sets(reader, buf)?;
                }
                b"typeDescriptor" => {
                    plugin.type_name = read_type_descriptor(reader, buf)?;
                }
                _ => {}
            },
            Ok(Event::Empty(e)) if e.name().as_ref() == b"image" => {
                plugin.image_path = get_attr(&e, "path");
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"plugin" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(plugin)
}

fn read_flag_sets(reader: &mut Reader<&[u8]>, buf: &mut Vec<u8>) -> Result<Vec<FlagSet>> {
    let mut flags = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) if e.name().as_ref() == b"flag" => {
                let name = get_attr(&e, "name").unwrap_or_default();
                let value = read_text(reader, buf)?;
                flags.push(FlagSet { name, value });
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"conditionFlags" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(flags)
}

fn read_type_descriptor(reader: &mut Reader<&[u8]>, buf: &mut Vec<u8>) -> Result<PluginType> {
    let mut result = PluginType::Optional;
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Empty(e)) if e.name().as_ref() == b"type" => {
                if let Some(name) = get_attr(&e, "name") {
                    result = PluginType::from_str(&name);
                }
            }
            Ok(Event::Start(e)) if e.name().as_ref() == b"defaultType" => {
                if let Some(name) = get_attr(&e, "name") {
                    result = PluginType::from_str(&name);
                }
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"typeDescriptor" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(result)
}

fn read_conditional_installs(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
) -> Result<Vec<ConditionalPattern>> {
    let mut patterns = Vec::new();
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) if e.name().as_ref() == b"pattern" => {
                patterns.push(read_conditional_pattern(reader, buf)?);
            }
            Ok(Event::End(e)) if e.name().as_ref() == b"conditionalFileInstalls" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(patterns)
}

fn read_conditional_pattern(
    reader: &mut Reader<&[u8]>,
    buf: &mut Vec<u8>,
) -> Result<ConditionalPattern> {
    let mut pattern = ConditionalPattern {
        conditions: Vec::new(),
        files: Vec::new(),
    };
    loop {
        match reader.read_event_into(buf) {
            Ok(Event::Start(e)) => match e.name().as_ref() {
                b"dependencies" => {
                    pattern.conditions = read_flag_conditions(reader, buf, b"dependencies")?;
                }
                b"files" => {
                    pattern.files = read_file_list(reader, buf, b"files")?;
                }
                _ => {}
            },
            Ok(Event::End(e)) if e.name().as_ref() == b"pattern" => break,
            Ok(Event::Eof) => break,
            _ => {}
        }
        buf.clear();
    }
    Ok(pattern)
}

fn get_attr(e: &quick_xml::events::BytesStart, name: &str) -> Option<String> {
    e.attributes()
        .flatten()
        .find(|a| a.key.as_ref() == name.as_bytes())
        .and_then(|a| a.unescape_value().ok().map(|v| v.into_owned()))
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    const SIMPLE_FOMOD: &str = r#"<?xml version="1.0" encoding="UTF-8"?>
<config>
    <moduleName>Test Mod</moduleName>
    <requiredInstallFiles>
        <file source="core/plugin.esp" destination="plugin.esp" />
        <folder source="core/textures" destination="textures" />
    </requiredInstallFiles>
    <installSteps order="Explicit">
        <installStep name="Choose Texture Quality">
            <optionalFileGroups order="Explicit">
                <group name="Texture Resolution" type="SelectExactlyOne">
                    <plugins order="Explicit">
                        <plugin name="High Quality">
                            <description>4K textures</description>
                            <files>
                                <folder source="optional/4k" destination="textures" />
                            </files>
                            <conditionFlags>
                                <flag name="quality">high</flag>
                            </conditionFlags>
                            <typeDescriptor>
                                <type name="Recommended" />
                            </typeDescriptor>
                        </plugin>
                        <plugin name="Low Quality">
                            <description>1K textures</description>
                            <files>
                                <folder source="optional/1k" destination="textures" />
                            </files>
                            <conditionFlags>
                                <flag name="quality">low</flag>
                            </conditionFlags>
                            <typeDescriptor>
                                <type name="Optional" />
                            </typeDescriptor>
                        </plugin>
                    </plugins>
                </group>
            </optionalFileGroups>
        </installStep>
    </installSteps>
    <conditionalFileInstalls>
        <patterns>
            <pattern>
                <dependencies>
                    <flagDependency flag="quality" value="high" />
                </dependencies>
                <files>
                    <file source="bonus/lod.dds" destination="textures/lod.dds" />
                </files>
            </pattern>
        </patterns>
    </conditionalFileInstalls>
</config>"#;

    #[test]
    fn test_parse_module_name() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        assert_eq!(config.module_name, "Test Mod");
    }

    #[test]
    fn test_parse_required_files() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        assert_eq!(config.required_files.len(), 2);
        assert_eq!(config.required_files[0].source, "core/plugin.esp");
        assert_eq!(config.required_files[0].destination, "plugin.esp");
        assert!(!config.required_files[0].is_folder);
        assert!(config.required_files[1].is_folder);
    }

    #[test]
    fn test_parse_install_steps() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        assert_eq!(config.install_steps.len(), 1);

        let step = &config.install_steps[0];
        assert_eq!(step.name, "Choose Texture Quality");
        assert_eq!(step.groups.len(), 1);

        let group = &step.groups[0];
        assert_eq!(group.name, "Texture Resolution");
        assert_eq!(group.group_type, GroupType::SelectExactlyOne);
        assert_eq!(group.plugins.len(), 2);

        assert_eq!(group.plugins[0].name, "High Quality");
        assert_eq!(group.plugins[0].description, "4K textures");
        assert_eq!(group.plugins[0].type_name, PluginType::Recommended);
        assert_eq!(group.plugins[0].condition_flags.len(), 1);
        assert_eq!(group.plugins[0].condition_flags[0].name, "quality");
        assert_eq!(group.plugins[0].condition_flags[0].value, "high");
    }

    #[test]
    fn test_parse_conditional_installs() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        assert_eq!(config.conditional_installs.len(), 1);
        assert_eq!(config.conditional_installs[0].conditions.len(), 1);
        assert_eq!(config.conditional_installs[0].files.len(), 1);
    }

    #[test]
    fn test_resolve_required_only() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        let selections = UserSelections::new();
        let actions = resolve_installation(&config, &selections);
        // Should have the 2 required files
        assert!(actions.len() >= 2);
    }

    #[test]
    fn test_resolve_with_selection() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        let mut selections = UserSelections::new();
        // Select "High Quality" (index 0) in step 0, group 0
        selections.insert((0, 0), vec![0]);

        let actions = resolve_installation(&config, &selections);
        // 2 required + 1 from high quality + 1 conditional (quality=high triggers bonus lod)
        assert_eq!(actions.len(), 4);

        // Check that the conditional file was included
        assert!(actions
            .iter()
            .any(|a| a.source == PathBuf::from("bonus/lod.dds")));
    }

    #[test]
    fn test_resolve_conditional_not_triggered() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        let mut selections = UserSelections::new();
        // Select "Low Quality" (index 1)
        selections.insert((0, 0), vec![1]);

        let actions = resolve_installation(&config, &selections);
        // 2 required + 1 from low quality, NO conditional (quality=low doesn't match quality=high)
        assert_eq!(actions.len(), 3);
        assert!(!actions
            .iter()
            .any(|a| a.source == PathBuf::from("bonus/lod.dds")));
    }

    #[test]
    fn test_visible_steps() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        let flags = HashMap::new();
        let visible = visible_steps(&config, &flags);
        // No visibility conditions, so all steps are visible
        assert_eq!(visible, vec![0]);
    }

    #[test]
    fn test_collect_flags() {
        let config = parse_module_config_str(SIMPLE_FOMOD).unwrap();
        let mut selections = UserSelections::new();
        selections.insert((0, 0), vec![0]); // High Quality
        let flags = collect_flags(&config, &selections);
        assert_eq!(flags.get("quality"), Some(&"high".to_string()));
    }
}

use std::path::{Path, PathBuf};
use std::process::{Child, Command};

use anyhow::{Context, Result};

/// Configuration for launching an executable.
#[derive(Debug, Clone)]
pub struct LaunchConfig {
    /// Path to the executable (.exe for Proton, or native binary).
    pub binary: PathBuf,
    /// Command-line arguments.
    pub arguments: Vec<String>,
    /// Working directory.
    pub working_dir: Option<PathBuf>,
    /// Proton installation path (if launching via Proton).
    pub proton_path: Option<PathBuf>,
    /// Wine prefix path (STEAM_COMPAT_DATA_PATH parent, i.e. compatdata/<appid>).
    pub prefix_path: Option<PathBuf>,
    /// Steam App ID for the game (used for STEAM_COMPAT_DATA_PATH and SteamAppId).
    pub steam_app_id: Option<u32>,
    /// Additional environment variables.
    pub env_vars: Vec<(String, String)>,
}

impl LaunchConfig {
    pub fn new(binary: impl Into<PathBuf>) -> Self {
        Self {
            binary: binary.into(),
            arguments: Vec::new(),
            working_dir: None,
            proton_path: None,
            prefix_path: None,
            steam_app_id: None,
            env_vars: Vec::new(),
        }
    }

    pub fn with_arguments(mut self, args: Vec<String>) -> Self {
        self.arguments = args;
        self
    }

    pub fn with_working_dir(mut self, dir: impl Into<PathBuf>) -> Self {
        self.working_dir = Some(dir.into());
        self
    }

    pub fn with_proton(mut self, proton_path: impl Into<PathBuf>) -> Self {
        self.proton_path = Some(proton_path.into());
        self
    }

    pub fn with_prefix(mut self, prefix_path: impl Into<PathBuf>) -> Self {
        self.prefix_path = Some(prefix_path.into());
        self
    }

    pub fn with_steam_app_id(mut self, app_id: u32) -> Self {
        self.steam_app_id = Some(app_id);
        self
    }

    pub fn with_env(mut self, key: impl Into<String>, value: impl Into<String>) -> Self {
        self.env_vars.push((key.into(), value.into()));
        self
    }
}

/// Launch an executable, either directly or via Proton.
pub fn launch(config: &LaunchConfig) -> Result<Child> {
    if let Some(proton_path) = &config.proton_path {
        launch_with_proton(config, proton_path)
    } else {
        launch_direct(config)
    }
}

/// Launch directly (native executable).
fn launch_direct(config: &LaunchConfig) -> Result<Child> {
    let mut cmd = Command::new(&config.binary);
    cmd.args(&config.arguments);

    if let Some(dir) = &config.working_dir {
        cmd.current_dir(dir);
    }

    for (key, value) in &config.env_vars {
        cmd.env(key, value);
    }

    cmd.spawn()
        .with_context(|| format!("Failed to launch {}", config.binary.display()))
}

/// Ensure Steam is running. If not, start it in silent mode.
fn ensure_steam_running() {
    use std::process::Stdio;

    // Check for steam or steamwebhelper process
    let steam_running = Command::new("pgrep")
        .arg("-x")
        .arg("steam")
        .stdout(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false);

    if !steam_running {
        tracing::warn!("Steam is not running — starting in silent mode");
        let _ = Command::new("steam")
            .arg("-silent")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn();
    }
}

/// Launch via Proton (`proton run <exe>`).
///
/// Required env vars for `proton run`:
/// - `WINEPREFIX` — points to `pfx/` directory
/// - `STEAM_COMPAT_DATA_PATH` — points to `compatdata/<appid>/` (parent of pfx/)
/// - `STEAM_COMPAT_CLIENT_INSTALL_PATH` — Steam installation directory
/// - `SteamAppId` / `SteamGameId` — numeric app ID
fn launch_with_proton(config: &LaunchConfig, proton_path: &Path) -> Result<Child> {
    // Steam must be running for DRM authentication
    ensure_steam_running();

    let proton_script = proton_path.join("proton");

    let mut cmd = Command::new(&proton_script);
    cmd.arg("run");
    cmd.arg(&config.binary);
    cmd.args(&config.arguments);

    if let Some(dir) = &config.working_dir {
        cmd.current_dir(dir);
    }

    // Proton required env vars
    let steam_path = nak_rust::steam::find_steam_path().unwrap_or_else(|| PathBuf::from("/home"));

    // Determine compat data path (compatdata/<appid>/) and WINEPREFIX (pfx/).
    // config.prefix_path points to pfx/ — compat data is its parent.
    let (compat_data, wine_prefix) = if let Some(prefix) = &config.prefix_path {
        let compat = prefix.parent().unwrap_or(prefix).to_path_buf();
        (Some(compat), Some(prefix.clone()))
    } else if let Some(app_id) = config.steam_app_id {
        let compat = steam_path
            .join("steamapps/compatdata")
            .join(app_id.to_string());
        let pfx = compat.join("pfx");
        (Some(compat), Some(pfx))
    } else {
        (None, None)
    };

    if let Some(ref prefix) = wine_prefix {
        cmd.env("WINEPREFIX", prefix);
    }

    if let Some(ref compat) = compat_data {
        cmd.env("STEAM_COMPAT_DATA_PATH", compat);
    }

    cmd.env("STEAM_COMPAT_CLIENT_INSTALL_PATH", &steam_path);

    if let Some(app_id) = config.steam_app_id {
        cmd.env("SteamAppId", app_id.to_string());
        cmd.env("SteamGameId", app_id.to_string());
    }

    // Extra env vars for compatibility
    cmd.env("DOTNET_ROOT", "");
    cmd.env("DOTNET_MULTILEVEL_LOOKUP", "0");

    for (key, value) in &config.env_vars {
        cmd.env(key, value);
    }

    tracing::info!(
        "Proton launch: {} run {} (prefix: {:?}, compat_data: {:?})",
        proton_script.display(),
        config.binary.display(),
        wine_prefix,
        compat_data,
    );

    cmd.spawn()
        .with_context(|| format!("Failed to launch {} via Proton", config.binary.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_launch_config_builder() {
        let config = LaunchConfig::new("/path/to/game.exe")
            .with_arguments(vec!["-forcesteamloader".to_string()])
            .with_working_dir("/path/to/game")
            .with_proton("/path/to/proton")
            .with_steam_app_id(489830)
            .with_env("WINEDLLOVERRIDES", "xaudio2_7=n,b");

        assert_eq!(config.binary, PathBuf::from("/path/to/game.exe"));
        assert_eq!(config.arguments, vec!["-forcesteamloader"]);
        assert_eq!(config.working_dir, Some(PathBuf::from("/path/to/game")));
        assert_eq!(config.proton_path, Some(PathBuf::from("/path/to/proton")));
        assert_eq!(config.steam_app_id, Some(489830));
        assert_eq!(config.env_vars.len(), 1);
        assert_eq!(
            config.env_vars[0],
            ("WINEDLLOVERRIDES".to_string(), "xaudio2_7=n,b".to_string())
        );
    }

    #[test]
    fn test_launch_config_defaults() {
        let config = LaunchConfig::new("/bin/echo");
        assert!(config.arguments.is_empty());
        assert!(config.working_dir.is_none());
        assert!(config.proton_path.is_none());
        assert!(config.prefix_path.is_none());
        assert!(config.steam_app_id.is_none());
        assert!(config.env_vars.is_empty());
    }
}

//! NXM protocol handler for Nexus Mods downloads.
//!
//! Registers as a handler for nxm:// links and uses a Unix domain socket
//! to receive download requests from browser clicks.

use std::io::Write;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::PathBuf;
use std::sync::mpsc;

use anyhow::{bail, Context, Result};

/// Well-known socket path for NXM IPC.
fn socket_path() -> PathBuf {
    if let Ok(runtime_dir) = std::env::var("XDG_RUNTIME_DIR") {
        PathBuf::from(runtime_dir).join("fluorine-manager-nxm.sock")
    } else {
        PathBuf::from("/tmp/fluorine-manager-nxm.sock")
    }
}

/// Parsed NXM link with auth credentials.
#[derive(Debug, Clone)]
pub struct NxmLink {
    pub game_domain: String,
    pub mod_id: u64,
    pub file_id: u64,
    pub key: String,
    pub expires: u64,
}

impl NxmLink {
    /// Parse an nxm:// URL into its components.
    /// Format: `nxm://game/mods/mod_id/files/file_id?key=xxx&expires=yyy`
    pub fn parse(url: &str) -> Result<Self> {
        let url = url
            .strip_prefix("nxm://")
            .context("URL must start with nxm://")?;

        let (path, query) = url.split_once('?').context("URL must have query params")?;

        let parts: Vec<&str> = path.split('/').collect();
        if parts.len() != 5 || parts[1] != "mods" || parts[3] != "files" {
            bail!("Invalid NXM path format: {}", path);
        }

        let game_domain = parts[0].to_string();
        let mod_id: u64 = parts[2].parse().context("Invalid mod_id")?;
        let file_id: u64 = parts[4].parse().context("Invalid file_id")?;

        let params: std::collections::HashMap<&str, &str> = query
            .split('&')
            .filter_map(|pair| pair.split_once('='))
            .collect();

        let key = params
            .get("key")
            .context("Missing 'key' param")?
            .to_string();
        let expires: u64 = params
            .get("expires")
            .context("Missing 'expires' param")?
            .parse()?;

        Ok(Self {
            game_domain,
            mod_id,
            file_id,
            key,
            expires,
        })
    }

    /// Create a unique lookup key for this link.
    pub fn lookup_key(&self) -> String {
        format!("{}:{}:{}", self.game_domain, self.mod_id, self.file_id)
    }
}

/// Send an NXM link to a running Fluorine Manager instance via Unix socket.
///
/// Used by the desktop handler subprocess (`fluorine-manager nxm-handle <url>`).
pub fn send_to_socket(nxm_url: &str) -> Result<()> {
    let path = socket_path();
    let result = (|| -> Result<()> {
        let mut stream = UnixStream::connect(&path)
            .with_context(|| format!("Failed to connect to NXM socket at {}", path.display()))?;
        writeln!(stream, "{}", nxm_url).context("Failed to write NXM URL to socket")?;
        Ok(())
    })();

    if let Err(ref e) = result {
        let log_path = path.with_extension("log");
        if let Ok(mut f) = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&log_path)
        {
            let _ = writeln!(f, "[nxm-handle] Error sending '{}': {:#}", nxm_url, e);
        }
    }

    result
}

/// Start the NXM Unix domain socket listener.
///
/// Returns a `Receiver<NxmLink>` and spawns a background thread that
/// accepts connections, reads one line (nxm:// URL), parses it, and sends
/// the result on the channel.
pub fn start_listener() -> Result<mpsc::Receiver<NxmLink>> {
    let (tx, rx) = mpsc::channel();
    let path = socket_path();

    // Remove stale socket file before binding
    if path.exists() {
        std::fs::remove_file(&path)
            .with_context(|| format!("Failed to remove stale socket: {}", path.display()))?;
    }

    let listener = UnixListener::bind(&path)
        .with_context(|| format!("Failed to bind Unix socket: {}", path.display()))?;

    tracing::info!("NXM handler listening on {}", path.display());

    // Set non-blocking so the thread can check for shutdown
    listener.set_nonblocking(false)?;

    std::thread::spawn(move || {
        for stream in listener.incoming() {
            let stream = match stream {
                Ok(s) => s,
                Err(e) => {
                    tracing::error!("NXM socket accept error: {}", e);
                    continue;
                }
            };

            let mut reader = std::io::BufReader::new(stream);
            let mut line = String::new();
            if let Err(e) = std::io::BufRead::read_line(&mut reader, &mut line) {
                tracing::error!("NXM socket read error: {}", e);
                continue;
            }

            let url = line.trim();
            if url.is_empty() {
                continue;
            }

            match NxmLink::parse(url) {
                Ok(link) => {
                    tracing::info!("Received NXM link: {}:{}", link.mod_id, link.file_id);
                    if tx.send(link).is_err() {
                        break; // Receiver dropped
                    }
                }
                Err(e) => {
                    tracing::error!("Invalid NXM link received: {}", e);
                }
            }
        }
    });

    Ok(rx)
}

// ============================================================================
// Nexus API Client
// ============================================================================

const NEXUS_API_BASE: &str = "https://api.nexusmods.com/v1";
const USER_AGENT: &str = concat!("FluorineManager/", env!("CARGO_PKG_VERSION"));

/// Validate that an API key works by checking the user's account.
/// Returns the username on success.
pub fn validate_api_key(api_key: &str) -> Result<String> {
    let resp: serde_json::Value = ureq::get(&format!("{NEXUS_API_BASE}/users/validate.json"))
        .set("apikey", api_key)
        .set("User-Agent", USER_AGENT)
        .call()
        .context("Nexus API request failed")?
        .into_json()
        .context("Failed to parse Nexus API response")?;

    let name = resp["name"].as_str().unwrap_or("Unknown").to_string();

    Ok(name)
}

/// Get the download URL for a file from an NXM link.
///
/// Returns a list of download URLs (mirrors). The first URL is usually best.
pub fn get_download_urls(api_key: &str, link: &NxmLink) -> Result<Vec<String>> {
    let url = format!(
        "{NEXUS_API_BASE}/games/{}/mods/{}/files/{}/download_link.json?key={}&expires={}",
        link.game_domain, link.mod_id, link.file_id, link.key, link.expires
    );

    let resp: serde_json::Value = ureq::get(&url)
        .set("apikey", api_key)
        .set("User-Agent", USER_AGENT)
        .call()
        .context("Nexus download link request failed")?
        .into_json()
        .context("Failed to parse download link response")?;

    let urls = resp
        .as_array()
        .context("Expected array of download links")?
        .iter()
        .filter_map(|entry| entry["URI"].as_str().map(|s| s.to_string()))
        .collect::<Vec<_>>();

    if urls.is_empty() {
        bail!("No download URLs returned from Nexus API");
    }

    Ok(urls)
}

/// Get file info (name, size, version) from the Nexus API.
#[derive(Debug, Clone)]
pub struct NexusFileInfo {
    pub name: String,
    pub file_name: String,
    pub size_kb: u64,
    pub version: String,
    pub mod_name: String,
}

/// Fetch file metadata from the Nexus API.
pub fn get_file_info(
    api_key: &str,
    game_domain: &str,
    mod_id: u64,
    file_id: u64,
) -> Result<NexusFileInfo> {
    // Get mod info for the mod name
    let mod_url = format!("{NEXUS_API_BASE}/games/{game_domain}/mods/{mod_id}.json");
    let mod_resp: serde_json::Value = ureq::get(&mod_url)
        .set("apikey", api_key)
        .set("User-Agent", USER_AGENT)
        .call()
        .context("Nexus mod info request failed")?
        .into_json()
        .context("Failed to parse mod info response")?;

    let mod_name = mod_resp["name"]
        .as_str()
        .unwrap_or("Unknown Mod")
        .to_string();

    // Get file info
    let file_url =
        format!("{NEXUS_API_BASE}/games/{game_domain}/mods/{mod_id}/files/{file_id}.json");
    let file_resp: serde_json::Value = ureq::get(&file_url)
        .set("apikey", api_key)
        .set("User-Agent", USER_AGENT)
        .call()
        .context("Nexus file info request failed")?
        .into_json()
        .context("Failed to parse file info response")?;

    Ok(NexusFileInfo {
        name: file_resp["name"].as_str().unwrap_or("").to_string(),
        file_name: file_resp["file_name"]
            .as_str()
            .unwrap_or("download")
            .to_string(),
        size_kb: file_resp["size_in_bytes"]
            .as_u64()
            .or_else(|| file_resp["size_kb"].as_u64().map(|kb| kb * 1024))
            .unwrap_or(0),
        version: file_resp["version"].as_str().unwrap_or("").to_string(),
        mod_name,
    })
}

/// Download a file from a URL to a destination path, reporting progress.
///
/// `progress_cb` is called with `(bytes_downloaded, total_bytes)`.
pub fn download_file(
    url: &str,
    dest: &std::path::Path,
    progress_cb: &dyn Fn(u64, u64),
) -> Result<()> {
    let resp = ureq::get(url)
        .set("User-Agent", USER_AGENT)
        .call()
        .context("Download request failed")?;

    let total = resp
        .header("content-length")
        .and_then(|s| s.parse::<u64>().ok())
        .unwrap_or(0);

    if let Some(parent) = dest.parent() {
        std::fs::create_dir_all(parent)
            .with_context(|| format!("Failed to create download dir {:?}", parent))?;
    }

    let mut file = std::fs::File::create(dest)
        .with_context(|| format!("Failed to create download file {:?}", dest))?;

    let mut reader = resp.into_reader();
    let mut downloaded: u64 = 0;
    let mut buf = [0u8; 65536];
    loop {
        let n = reader
            .read(&mut buf)
            .context("Read error during download")?;
        if n == 0 {
            break;
        }
        std::io::Write::write_all(&mut file, &buf[..n]).context("Write error during download")?;
        downloaded += n as u64;
        progress_cb(downloaded, total);
    }

    Ok(())
}

/// Write a MO2-compatible .meta file alongside a downloaded mod archive.
pub fn write_meta_file(
    archive_path: &std::path::Path,
    link: &NxmLink,
    file_info: &NexusFileInfo,
) -> Result<()> {
    // MO2 meta files are named "<archive>.meta" (appended, not replacing extension)
    let mut meta_name = archive_path.as_os_str().to_os_string();
    meta_name.push(".meta");
    let meta_path = std::path::PathBuf::from(meta_name);

    let content = format!(
        "[General]\n\
         gameName={}\n\
         modID={}\n\
         fileID={}\n\
         url=\n\
         modName={}\n\
         version={}\n\
         newestVersion=\n\
         category=\n\
         installationFile={}\n\
         repository=Nexus\n",
        link.game_domain,
        link.mod_id,
        link.file_id,
        file_info.mod_name,
        file_info.version,
        file_info.file_name,
    );

    std::fs::write(&meta_path, &content)
        .with_context(|| format!("Failed to write meta file {:?}", meta_path))?;

    Ok(())
}

/// Register this application as the system handler for nxm:// protocol.
pub fn register_handler() -> Result<()> {
    let exe_path = std::env::current_exe().context("Failed to get current executable path")?;

    let home = std::env::var("HOME").context("HOME not set")?;

    // Create a wrapper script at a path without spaces — xdg-mime's
    // desktop_file_to_binary() uses shell `read` to split the Exec line on
    // whitespace, so paths containing spaces break binary validation and the
    // handler registration silently falls through to the mimeinfo.cache.
    let bin_dir = format!("{}/.local/bin", home);
    std::fs::create_dir_all(&bin_dir).context("Failed to create ~/.local/bin")?;

    let wrapper_path = format!("{}/fluorine-manager-nxm", bin_dir);
    let wrapper_script = format!("#!/bin/sh\nexec \"{}\" \"$@\"\n", exe_path.display());
    std::fs::write(&wrapper_path, &wrapper_script)
        .with_context(|| format!("Failed to write wrapper script {}", wrapper_path))?;

    // Make wrapper executable
    use std::os::unix::fs::PermissionsExt;
    std::fs::set_permissions(&wrapper_path, std::fs::Permissions::from_mode(0o755))
        .with_context(|| format!("Failed to chmod {}", wrapper_path))?;

    // Desktop file uses the wrapper (no spaces, no quotes needed)
    let desktop_entry = "[Desktop Entry]\n\
        Type=Application\n\
        Name=Fluorine Manager NXM Handler\n\
        Exec=fluorine-manager-nxm nxm-handle %u\n\
        MimeType=x-scheme-handler/nxm;\n\
        NoDisplay=true\n";

    let apps_dir = format!("{}/.local/share/applications", home);
    let desktop_path = format!("{}/fluorine-manager-nxm.desktop", apps_dir);

    std::fs::create_dir_all(&apps_dir).context("Failed to create applications directory")?;
    std::fs::write(&desktop_path, desktop_entry)
        .with_context(|| format!("Failed to write {}", desktop_path))?;

    // Update both mimeapps.list files directly
    let local_mimeapps = format!("{}/mimeapps.list", apps_dir);
    update_mimeapps_list(
        &local_mimeapps,
        "x-scheme-handler/nxm",
        "fluorine-manager-nxm.desktop",
    )?;

    let config_dir = format!("{}/.config", home);
    std::fs::create_dir_all(&config_dir).ok();
    let config_mimeapps = format!("{}/mimeapps.list", config_dir);
    update_mimeapps_list(
        &config_mimeapps,
        "x-scheme-handler/nxm",
        "fluorine-manager-nxm.desktop",
    )?;

    // Rebuild mimeinfo.cache so xdg-mime picks up the new desktop file
    let _ = std::process::Command::new("update-desktop-database")
        .arg(&apps_dir)
        .status();

    tracing::info!(
        "Registered as nxm:// handler (desktop file: {})",
        desktop_path
    );
    Ok(())
}

/// Update or create a mimeapps.list file to set a handler for a mime type.
fn update_mimeapps_list(path: &str, mime_type: &str, desktop_file: &str) -> Result<()> {
    let content = std::fs::read_to_string(path).unwrap_or_default();
    let entry = format!("{}={}", mime_type, desktop_file);

    let mut lines: Vec<String> = Vec::new();
    let mut in_default_section = false;
    let mut found = false;

    for line in content.lines() {
        let trimmed = line.trim();

        if trimmed.starts_with('[') {
            in_default_section =
                trimmed == "[Default Applications]" || trimmed == "[Added Associations]";
        }

        if in_default_section && trimmed.starts_with(&format!("{}=", mime_type)) {
            lines.push(entry.clone());
            found = true;
        } else {
            lines.push(line.to_string());
        }
    }

    if !found {
        // Add under [Default Applications] or create the section
        if content.contains("[Default Applications]") {
            let mut new_lines = Vec::new();
            for line in &lines {
                new_lines.push(line.clone());
                if line.trim() == "[Default Applications]" {
                    new_lines.push(entry.clone());
                }
            }
            lines = new_lines;
        } else {
            if !lines.is_empty() && !lines.last().unwrap().is_empty() {
                lines.push(String::new());
            }
            lines.push("[Default Applications]".to_string());
            lines.push(entry);
        }
    }

    std::fs::write(path, lines.join("\n") + "\n")
        .with_context(|| format!("Failed to write {}", path))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_nxm_link() {
        let url = "nxm://skyrimspecialedition/mods/12345/files/67890?key=abc123&expires=9999999999&user_id=42";
        let link = NxmLink::parse(url).unwrap();
        assert_eq!(link.game_domain, "skyrimspecialedition");
        assert_eq!(link.mod_id, 12345);
        assert_eq!(link.file_id, 67890);
        assert_eq!(link.key, "abc123");
        assert_eq!(link.expires, 9999999999);
    }

    #[test]
    fn test_parse_nxm_link_minimal() {
        let url = "nxm://fallout4/mods/1/files/2?key=k&expires=0";
        let link = NxmLink::parse(url).unwrap();
        assert_eq!(link.game_domain, "fallout4");
        assert_eq!(link.mod_id, 1);
        assert_eq!(link.file_id, 2);
    }

    #[test]
    fn test_parse_nxm_invalid() {
        assert!(NxmLink::parse("https://example.com").is_err());
        assert!(NxmLink::parse("nxm://game/invalid").is_err());
        assert!(NxmLink::parse("nxm://game/mods/1/files/2").is_err()); // no query
    }

    #[test]
    fn test_lookup_key() {
        let url = "nxm://skyrimspecialedition/mods/12345/files/67890?key=abc&expires=999";
        let link = NxmLink::parse(url).unwrap();
        assert_eq!(link.lookup_key(), "skyrimspecialedition:12345:67890");
    }

    #[test]
    fn test_socket_path() {
        let path = socket_path();
        assert!(path.to_string_lossy().contains("fluorine-manager-nxm.sock"));
    }
}

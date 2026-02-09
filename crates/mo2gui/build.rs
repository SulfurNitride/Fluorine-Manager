use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    slint_build::compile("ui/app.slint").unwrap();
    maybe_bundle_binary("7zz");
    maybe_bundle_binary("umu-run");
}

fn maybe_bundle_binary(binary_name: &str) {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap_or_default());
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap_or_default());

    let source = [
        manifest_dir.join(format!("bin/{binary_name}")),
        manifest_dir.join(format!("../bin/{binary_name}")),
        manifest_dir.join(format!("../../bin/{binary_name}")),
    ]
    .into_iter()
    .find(|p| p.exists() && p.is_file());

    let Some(source) = source else {
        println!(
            "cargo:warning=No bundled {} found (expected at crates/mo2gui/bin/{} or workspace bin/{})",
            binary_name, binary_name, binary_name
        );
        return;
    };

    // OUT_DIR is usually: <target>/<profile>/build/<pkg-hash>/out
    let profile_dir = out_dir
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .map(Path::to_path_buf);

    let Some(profile_dir) = profile_dir else {
        println!(
            "cargo:warning=Could not infer target profile dir from OUT_DIR; skipping {} bundling",
            binary_name
        );
        return;
    };

    let dest_dir = profile_dir.join("bin");
    let dest = dest_dir.join(binary_name);

    if let Err(e) = fs::create_dir_all(&dest_dir) {
        println!(
            "cargo:warning=Failed creating bundled {} dir {}: {e}",
            binary_name,
            dest_dir.display()
        );
        return;
    }

    match fs::copy(&source, &dest) {
        Ok(_) => {
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                if let Ok(meta) = fs::metadata(&dest) {
                    let mut perms = meta.permissions();
                    perms.set_mode(0o755);
                    let _ = fs::set_permissions(&dest, perms);
                }
            }
            println!(
                "cargo:warning=Bundled {}: {} -> {}",
                binary_name,
                source.display(),
                dest.display()
            );
        }
        Err(e) => {
            println!(
                "cargo:warning=Failed copying bundled {} {} -> {}: {e}",
                binary_name,
                source.display(),
                dest.display()
            );
        }
    }
}

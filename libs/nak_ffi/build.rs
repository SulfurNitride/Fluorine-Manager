fn main() {
    // Set SONAME so the dynamic linker can identify this library by name.
    // Without SONAME, dlopen'd plugins each load a separate instance,
    // defeating Rust static caches (like the game detection cache).
    println!("cargo:rustc-cdylib-link-arg=-Wl,-soname,libnak_ffi.so");
}

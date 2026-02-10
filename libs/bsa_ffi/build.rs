fn main() {
    println!("cargo:rustc-cdylib-link-arg=-Wl,-soname,libbsa_ffi.so");
}

use std::ffi::CString;
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn photo_editor_rust_version() -> *mut c_char {
    CString::new(env!("CARGO_PKG_VERSION"))
        .expect("version string should be valid")
        .into_raw()
}

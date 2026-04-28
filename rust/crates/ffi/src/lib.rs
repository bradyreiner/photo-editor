use std::ffi::CString;
use std::os::raw::c_char;

#[no_mangle]
pub extern "C" fn photo_editor_rust_version() -> *mut c_char {
    CString::new(env!("CARGO_PKG_VERSION"))
        .expect("version string should be valid")
        .into_raw()
}

/// # Safety
///
/// `ptr` must have been returned by `photo_editor_rust_version` and must not
/// be used after this call.
#[no_mangle]
pub unsafe extern "C" fn photo_editor_rust_string_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe { drop(CString::from_raw(ptr)) };
    }
}

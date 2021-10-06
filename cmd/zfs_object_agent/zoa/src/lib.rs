use std::ffi::CStr;
use std::os::raw::c_char;
use zettaobject::init;

/// # Safety
/// The pointers must be to actual C strings.
#[no_mangle]
pub unsafe extern "C" fn libzoa_init(
    socket_dir_ptr: *const c_char,
    log_file_ptr: *const c_char,
    cache_path_ptr: *const c_char,
) {
    let socket_dir = CStr::from_ptr(socket_dir_ptr)
        .to_string_lossy()
        .into_owned();
    let log_file = CStr::from_ptr(log_file_ptr).to_string_lossy().into_owned();

    let verbosity = 2;
    init::setup_logging(verbosity, Some(log_file.as_str()), None);

    if cache_path_ptr.is_null() {
        init::start(&socket_dir, None);
    } else {
        let cache = CStr::from_ptr(cache_path_ptr)
            .to_string_lossy()
            .into_owned();
        init::start(&socket_dir, Some(cache.as_str()));
    }
}

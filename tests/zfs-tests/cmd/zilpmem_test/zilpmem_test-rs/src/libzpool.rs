use std::sync::Mutex;
use std::str::FromStr;

pub use bindings as sys;

pub mod zfs_btree;
pub mod zfs_pmem;
pub mod zilpmem;

lazy_static::lazy_static! {
    static ref REFCOUNT: Mutex<usize> = {
        Mutex::default()
    };
}

/// There is no RAII handle concept because we'd need to pass
/// the handle as an argument to all libzpool functions in order to
/// get the lifetimes right. Otherwise, if the last expression in a function
/// relies on the lib being initlialized, things break in non-obvious ways
/// because Rust considers the last expression in a function a temporary
/// and drops all of the function's local variables, including the
/// fictional RAII deinit guard object, prior to evaluating the last expression.
/// Read the note at https://doc.rust-lang.org/reference/destructors.html#temporary-scopes
pub fn init_once() {
    let mut rc = REFCOUNT.lock().unwrap();
    if *rc == 0 {
        use std::convert::TryInto;
        unsafe {
            bindings::kernel_init(bindings::spa_mode_SPA_MODE_READ.try_into().unwrap());

            let env_var = "ZFS_ZILPMEM_TEST_OPS";
            match std::env::var(env_var) {
                Ok(v) => {
                    let ops =zfs_pmem::Ops::from_str(&v).expect(&format!("env var {} is not a valid pmem op", env_var));
                    bindings::zfs_pmem_ops_set(ops.into_raw());
                },
                Err(std::env::VarError::NotPresent) => (),
                Err(e) => panic!("env var {}: {}", env_var, e),
            }

        }
    }

    *rc += 1;
}

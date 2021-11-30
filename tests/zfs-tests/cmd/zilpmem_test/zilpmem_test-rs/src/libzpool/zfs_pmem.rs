use anyhow::{anyhow, Context};
use std::ffi::CStr;
use std::ffi::CString;
use std::fmt;
use std::str::FromStr;

/// All methods require zfs_pmem_ops is initialized.
/// Call [bindings::zfs_pmem_ops_init] to do that.
/// We do our best to panic if you forget.
#[derive(Copy, Clone)]
pub struct Ops(*const bindings::zfs_pmem_ops);

impl Ops {
    fn by_name<N: AsRef<CStr>>(name: N) -> Option<Self> {
        let o = unsafe { bindings::zfs_pmem_ops_get_by_name(name.as_ref().as_ptr()) };
        if o == std::ptr::null() {
            None
        } else {
            Some(Ops(o))
        }
    }
    pub fn into_raw(self) -> *const bindings::zfs_pmem_ops {
        self.0
    }
    pub fn name_cstr(&self) -> &'static CStr {
        unsafe { CStr::from_ptr(bindings::zfs_pmem_ops_name(self.0)) }
    }
    pub fn current() -> Self {
        let p = unsafe { bindings::zfs_pmem_ops_get_current() };
        assert_ne!(std::ptr::null(), p, "likely zfs_pmem_ops_init() missing");
        Ops(p)
    }
}

impl fmt::Display for Ops {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{:?}", self.name_cstr())
    }
}

impl FromStr for Ops {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let cs = CString::new(s).context("convert op name to CString")?;
        let cur = Self::by_name(cs).ok_or(anyhow!(format!("unknown pmem op name {:?}", s)))?;
        assert_ne!(
            std::ptr::null(),
            cur.0,
            "likely zfs_pmem_ops_init() missing"
        );
        Ok(cur)
    }
}

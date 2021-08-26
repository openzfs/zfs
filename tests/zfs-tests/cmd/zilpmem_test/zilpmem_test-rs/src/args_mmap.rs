use anyhow::Context;
use core::ffi::c_void;
use std::os::unix::io::IntoRawFd;
use std::{fs::OpenOptions, path::PathBuf};
use structopt::StructOpt;

#[derive(StructOpt, serde::Serialize)]
pub struct MmapArgs {
    #[structopt(long = "mmap-size")]
    pub size: usize,
    #[structopt(long = "mmap-path")]
    pub path: Option<PathBuf>,
}

impl MmapArgs {
    pub fn mmap(&self) -> anyhow::Result<&'static mut [u8]> {
        nix::errno::Errno::clear();
        let start: *mut c_void = if let Some(dax_mmap_path) = &self.path {
            let fd = OpenOptions::new()
                .write(true)
                .read(true)
                .truncate(true)
                .open(dax_mmap_path)
                .context("open path")?
                .into_raw_fd();
            unsafe {
                libc::mmap(
                    std::ptr::null_mut(),
                    self.size,
                    libc::PROT_READ | libc::PROT_WRITE,
                    libc::MAP_SHARED | libc::MAP_HUGE_1GB | libc::MAP_LOCKED | libc::MAP_POPULATE,
                    fd,
                    0,
                )
            }
        } else {
            nix::errno::Errno::clear();
            unsafe {
                libc::mmap(
                    std::ptr::null_mut(),
                    self.size,
                    libc::PROT_READ | libc::PROT_WRITE,
                    libc::MAP_PRIVATE
                        | libc::MAP_ANON
                        // | libc::MAP_HUGE_1GB
                        // | libc::MAP_LOCKED
                        | libc::MAP_POPULATE,
                    -1,
                    0,
                )
            }
        };

        if start == libc::MAP_FAILED {
            return Err(anyhow::anyhow!("mmap anon: {}", nix::errno::Errno::last()));
        }
        let start = start as *mut _ as *mut u8;
        return Ok(unsafe { std::slice::from_raw_parts_mut(start, self.size) });
    }
}

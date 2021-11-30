use zilpmem_test::*;

mod cmd_bench;

use libzpool::zfs_pmem;
use std::convert::TryInto;
use std::ffi::CString;
use structopt::StructOpt;

#[derive(StructOpt)]
struct App {
    /// This option only exists for documentation purposes. Set ZFS_DEBUG
    /// to the values expected by dprintf_setup / dprintf_find_string.
    /// Off by default because zfs_dbgmsg prints to stdout not stderr which
    /// should just be changed to stderr in the future.
    /// Example value is 'on' for all output.
    #[structopt(long("zfs-dbgmsg-enable"))]
    zfs_dbgmsg_enable: bool,

    #[structopt(long("pmem-ops"))]
    pmem_ops: Option<zfs_pmem::Ops>,

    #[structopt(long("fletcher4-ops"))]
    fletcher4_ops: String,

    #[structopt(subcommand)]
    cmd: Cmd,
}

#[derive(StructOpt)]
enum Cmd {
    Bench(cmd_bench::Args),
}

fn main() {
    // Initialize libzpool
    unsafe {
        // setup dprintf before kernel_init so that we get dbgmsg output
        // if requrest via ZFS_DEBUG env var
        let mut argv = [];
        let mut argc: i32 = argv.len().try_into().unwrap();
        libzpool::sys::dprintf_setup(&mut argc as *mut i32, argv.as_mut_ptr());

        /* setup libspl */
        libzpool::init_once();
    };

    // we can only start parsing arguments now because zfs_pmem::Ops requires
    // that bindings::kernel_init has been called
    let app = App::from_args();

    if app.zfs_dbgmsg_enable {
        panic!("do this via ZFS_DEBUG, read the docstring for this flag");
    }

    if let Some(ops) = &app.pmem_ops {
        unsafe {
            bindings::zfs_pmem_ops_set(ops.clone().into_raw());
        }
    }

    let f4ops = CString::new(app.fletcher4_ops.as_str()).unwrap();
    let err = unsafe { bindings::fletcher_4_impl_set(f4ops.as_ptr()) };
    if err != 0 {
        panic!("invalid fletcher4 ops value {:?}", app.fletcher4_ops);
    }

    match app.cmd {
        Cmd::Bench(a) => cmd_bench::run(a).unwrap(),
    }
}

use std::ffi::{c_void, CStr, CString};
use std::sync;
use std::sync::mpsc::{self, SyncSender};
use std::thread::JoinHandle;
use sync::Mutex;

lazy_static::lazy_static! {
    static ref ACTIVE_TEST: Mutex<()> = Mutex::new(());
}

#[macro_export]
macro_rules! crashtest {
    ($name:ident, $contains:literal, $crashtest_body:expr) => {
        #[test]
        pub fn $name() {
            let contains: &str = $contains;
            crate::crashtest::do_crashtest($crashtest_body, move |msg| {
                let msg = msg
                    .to_str()
                    .expect("msg should be convertible to Rust string");
                assert!(msg.contains(contains));
            });
        }
    };
}

enum TestThreadResult {
    Finishing,
    BodyPanicked,
    AbortHandlerCalled(CString, SyncSender<()>),
}
struct AbortHandlerArg {
    sender: SyncSender<TestThreadResult>,
}

pub fn do_crashtest<F, C>(body: F, checkmsg: C)
where
    F: 'static + FnOnce() + Send + std::panic::UnwindSafe,
    C: 'static + FnOnce(CString),
{
    zilpmem_test::libzpool::init_once();

    let active_test = ACTIVE_TEST
        .lock()
        .expect("likely another crashtest panicked");

    let (sender, receiver) = mpsc::sync_channel(0);

    let aha: *mut _ = Box::into_raw(Box::new(AbortHandlerArg {
        sender: sender.clone(),
    }));

    unsafe {
        bindings::libspl_set_alternative_abort_handler(Some(alt_abrt_handler), aha as *mut c_void)
    };

    extern "C" fn alt_abrt_handler(arg: *mut c_void, formatted: *mut i8) {
        let aha: &'static mut AbortHandlerArg = unsafe { &mut *(arg as *mut AbortHandlerArg) };

        let formatted_cstring = unsafe { CStr::from_ptr(formatted).to_owned() };
        unsafe { libc::free(formatted as *mut _ as *mut c_void) }

        let (confirm, waitconfirm) = mpsc::sync_channel(0);
        // NOTE: due to some cargo bug the eprintlns are not visible in cargo test, only when running the test binary directly
        if let Err(e) = aha.sender.send(TestThreadResult::AbortHandlerCalled(
            formatted_cstring,
            confirm,
        )) {
            eprintln!("error sending TestThreadResult::AbortHandlerCalled: {}", e);
            std::process::abort(); // must not unwind due to FFI boundary
        }
        // infinitely wait for main thread
        if let Err(e) = waitconfirm.recv() {
            eprintln!(
                "unexpectedly recv error for confirm from main thread: {:?}",
                e
            );
        }
        // unsafe { libc::pthread_exit(std::ptr::null_mut()) };
        // the next line should be unreachable
        std::process::abort(); // must not unwind due to FFI boundary
    }

    let testthread: JoinHandle<()> = std::thread::spawn({
        let sender = sender.clone();
        move || {
            let res = std::panic::catch_unwind(body);
            match res {
                Ok(()) => {
                    if let Err(e) = sender.send(TestThreadResult::Finishing) {
                        eprintln!("error sending TestThreadResult::Finishing: {}", e);
                        std::process::abort();
                    }
                }
                Err(_cause) => {
                    let res = sender.send(TestThreadResult::BodyPanicked);
                    if let Err(e) = res {
                        eprintln!("error sending TestThreadResult::BodyPanicked: {}", e);
                        std::process::abort();
                    }
                }
            }
        }
    });

    let res = receiver.recv().expect("receive result from test thread");

    // allow the other crash tests to run before we evaluate the result
    unsafe { bindings::libspl_set_alternative_abort_handler(None, std::ptr::null_mut()) };
    drop(active_test);

    match res {
        TestThreadResult::Finishing => {
            testthread.join().unwrap();
            unsafe { Box::from_raw(aha) }; // reclaim resources
            panic!("crashtest closure finished without failing a libspl assertion");
        }
        TestThreadResult::BodyPanicked => {
            testthread.join().unwrap();
            unsafe { Box::from_raw(aha) }; // reclaim resources
            panic!("crashtest closure panicked");
        }
        TestThreadResult::AbortHandlerCalled(msg, confirm) => {
            // keep testthread blocked forever, leaking the thread
            Box::leak(Box::new(confirm));
            // Box::leak(Box::new(receiver)); // leak receiver so that it's notnever closed

            // let err = unsafe { libc::pthread_cancel(testthread.into_pthread_t()) };
            // assert_eq!(err, 0);
            // pthread_cancel is not synchronous => leak all memory still accessible by testthread
            //
            // confirm.send(()).unwrap(); // confirm to receiver that we got the assert msg and tell it to pthread_exit
            //  don't reclaim the AbortHandlerArg, the thread's st.sender.send call still accesses it while we receive this message

            println!("msg: {:?}", msg);
            checkmsg(msg);
        }
    }
}

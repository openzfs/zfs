use bindings::entry_header_t;
use core::ffi::c_void;
use zilpmem_test::prb_test::util::*;

#[macro_use]
mod crashtest;

crashtest!(replay_done_on_unclaimed, "unexpected state", || {
    let prb = Prb::new_anon_with_chunksize(4, 1 << 20, 1);
    let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.prb, 1) };
    unsafe {
        let mut hdr = std::mem::MaybeUninit::uninit();
        bindings::zilpmem_prb_replay_done(hdl, hdr.as_mut_ptr());
    }
});

crashtest!(start_log_on_unclaimed, "unexpected state", || {
    let prb = Prb::new_anon_with_chunksize(4, 1 << 20, 1);
    let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.prb, 1) };
    unsafe {
        let mut hdr = std::mem::MaybeUninit::uninit();
        bindings::zilpmem_prb_create_log_if_not_exists(hdl, hdr.as_mut_ptr());
    }
});

// macro because of prb's lifetime
macro_rules! setup_claimed {
    ($prb:ident, $hdl:ident) => {
        let $prb = Prb::new_anon_with_chunksize(4, 1 << 20, 1);
        let $hdl = unsafe { bindings::zilpmem_prb_setup_objset($prb.prb, 1) };
        let mut needs_claiming = |_rn| {
            panic!("prb is empty, expecting no entries to be found");
        };
        let mut claim = |_rn| {
            panic!("prb is empty, expecting no entries to be found");
        };
        let mut cs = MockClaimstore::new(&mut needs_claiming, &mut claim);
        let (csi, csi_arg) = cs.as_claimstore_interface();
        unsafe {
            let mut hdr = std::mem::MaybeUninit::zeroed();
            bindings::zil_header_pmem_init(hdr.as_mut_ptr());
            let mut hdr = hdr.assume_init();

            let res = bindings::zilpmem_prb_claim($hdl, &mut hdr, 1, csi, csi_arg);
            assert_eq!(
                res.what,
                bindings::zilpmem_prb_claim_result_kind_PRB_CLAIM_RES_OK
            );
        }
    };
}

macro_rules! do_write {
    ($hdl:ident, $txg:expr) => {
        let body: u8 = 23;
        unsafe {
            bindings::zilpmem_prb_write_entry(
                $hdl,
                $txg,
                true as bindings::boolean_t,
                std::mem::size_of_val(&body) as u64,
                &body as *const _ as *const c_void,
            );
        }
    };
}

crashtest!(write_on_unreplayed, "unexpected state", || {
    setup_claimed!(prb, hdl);
    do_write!(hdl, 10);
});

crashtest!(
    write_on_replay_started_but_not_done,
    "unexpected state",
    || {
        setup_claimed!(prb, hdl);

        let mut replay = |_rn, _hdr| {
            panic!("prb is empty, not expecting any replay callback invocations");
        };
        let mut mr = MockReplay::new(&mut replay);
        let (rcb, rarg) = mr.as_arg();

        unsafe {
            let res = bindings::zilpmem_prb_replay(hdl, rcb, rarg);
            assert_eq!(
                res.what,
                bindings::zilpmem_prb_replay_result_kind_PRB_REPLAY_RES_OK
            );
        }

        do_write!(hdl, 10); // panic
    }
);

crashtest!(
    replay_on_unclaimed,
    "unexpected state",
    || {
        let prb = Prb::new_anon_with_chunksize(4, 1 << 20, 1);
        let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.prb, 1) };

        let mut replay = |_rn, _hdr| {
            panic!("prb is empty, not expecting any replay callback invocations");
        };
        let mut mr = MockReplay::new(&mut replay);
        let (rcb, rarg) = mr.as_arg();
        unsafe {
            bindings::zilpmem_prb_replay(hdl, rcb, rarg);
            unreachable!()
        }
    }
);

crashtest!(
    teardown_after_claiming_without_abandon_but_no_gc,
    "promised_no_more_gc",
    || {
        setup_claimed!(prb, hdl);
        unsafe {
            bindings::zilpmem_prb_teardown_objset(
                hdl,
                bindings::boolean_t_B_FALSE,
                std::ptr::null_mut(),
            )
        };
    }
);

crashtest!(gc_after_promising_no_more_gc, "promised_no_more_gc", || {
    let prb = Prb::new_anon_with_chunksize(4, 1 << 20, 1);
    unsafe { bindings::zilpmem_prb_promise_no_more_gc(prb.prb) };
    unsafe { bindings::zilpmem_prb_gc(prb.prb, 10) };
});

crashtest!(setup_same_objset_twice, "objset already set up", || {
    let prb = Prb::new_anon_with_chunksize(4, 1 << 20, 1);
    unsafe {
        bindings::zilpmem_prb_setup_objset(prb.prb, 23);
        bindings::zilpmem_prb_setup_objset(prb.prb, 23);
    }
});

crashtest!(
    too_large_writes,
    "caller must not request allocations larger than the smallest chunk",
    || {
        let chunksize = 1 << 20;
        let header_size = std::mem::size_of::<entry_header_t>();

        let prb = Prb::new_anon_with_chunksize(1, chunksize, 1);
        let hdl = unsafe {
            let hdl = bindings::zilpmem_prb_setup_objset(prb.prb, 23);
            let mut hdr = std::mem::MaybeUninit::uninit();
            bindings::zilpmem_prb_destroy_log(hdl, hdr.as_mut_ptr());
            bindings::zilpmem_prb_create_log_if_not_exists(hdl, hdr.as_mut_ptr());
            hdl
        };

        let mut body = bytes::BytesMut::new();
        body.resize(chunksize, 0);
        assert!(body.len() + header_size > chunksize);
        unsafe {
            bindings::zilpmem_prb_write_entry(
                hdl,
                2,
                true as bindings::boolean_t,
                body.len() as u64,
                body.as_ptr() as *const c_void,
            );
        }
    }
);

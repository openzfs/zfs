use super::util::*;
use bindings::boolean_t;
use bindings::boolean_t_B_TRUE;
use bindings::prb_write_result_t;
use bindings::prb_write_result_t_PRB_WRITE_OBSOLETE;
use bindings::prb_write_stats_t;
use bindings::zil_header_pmem_t;
use byteorder::ByteOrder;
use core::ffi::c_void;
use read_replay_node;
use std::collections::BTreeMap;

#[test]
fn prb_api_walkthrough() {
    libzpool::init_once();

    let mut chunk_data = HeapChunkData::new_zeroed(1 << 20);
    let chunk = alloc_heap_chunk(&mut chunk_data);
    let objset_id = 1;

    // write two entries
    let mut hdr = {
        let mut hdr = std::mem::MaybeUninit::uninit();
        let mut prb = Prb::new_empty(1);
        prb.free_chunks = false;
        prb.add_chunk_for_write(&chunk);

        let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.prb, objset_id) };
        unsafe {
            bindings::zilpmem_prb_destroy_log(hdl, hdr.as_mut_ptr());
            bindings::zilpmem_prb_create_log_if_not_exists(hdl, hdr.as_mut_ptr());
        }

        let body: u64 = 23;
        unsafe {
            bindings::zilpmem_prb_write_entry(
                hdl,
                2,
                true as bindings::boolean_t,
                std::mem::size_of_val(&body) as u64,
                &body as *const _ as *const c_void,
            );
        }

        let body: u64 = 42;
        unsafe {
            bindings::zilpmem_prb_write_entry(
                hdl,
                2,
                true as bindings::boolean_t,
                std::mem::size_of_val(&body) as u64,
                &body as *const _ as *const c_void,
            );
        }

        unsafe {
            bindings::zilpmem_prb_promise_no_more_gc(prb.prb);
            bindings::zilpmem_prb_teardown_objset(hdl, false as boolean_t, std::ptr::null_mut());
        }
        unsafe { hdr.assume_init() }
    };

    // now claim + replay

    let mut prb = Prb::new_empty(1);
    prb.free_chunks = true;
    prb.add_chunk_for_claim(&chunk);
    let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.prb, objset_id) };

    let mut claims = 0;
    let mut needs_claiming = |rn| {
        claims += 1;
        let rn = unsafe { read_replay_node(rn).unwrap() };
        use byteorder::ByteOrder;
        let v = byteorder::NativeEndian::read_u64(&rn.body[0..8]);
        Ok(match v {
            23 => true,
            42 => false,
            x => panic!("unexpected body value {}", x),
        })
    };
    let mut claim = |rn| {
        let rn = unsafe { read_replay_node(rn).unwrap() };
        let v = byteorder::NativeEndian::read_u64(&rn.body[0..8]);
        assert_eq!(v, 23);
        Ok(())
    };
    let mut cs = MockClaimstore::new(&mut needs_claiming, &mut claim);
    let (csi, csi_arg) = cs.as_claimstore_interface();
    unsafe {
        let res = bindings::zilpmem_prb_claim(hdl, &mut hdr, 1, csi, csi_arg);
        assert_eq!(
            res.what,
            bindings::zilpmem_prb_claim_result_kind_PRB_CLAIM_RES_OK
        );
    }

    assert_eq!(claims, 2);

    let mut replay_log = Vec::new();

    let mut replay = |rn, zh: *const zil_header_pmem_t| {
        let rn = unsafe { read_replay_node(rn).unwrap() };
        let zh = unsafe { *zh.as_zh_pmem_impl_ptr() };
        replay_log.push((rn, zh));
        0
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

    assert_eq!(replay_log.len(), claims);
    println!("replay log:\n{:#?}", replay_log);

    let bodies: Vec<_> = replay_log
        .iter()
        .map(|(ReplayNodeReading { body, .. }, _)| body)
        .enumerate()
        .collect();
    println!("bodies: {:?}", bodies);

    let bodies: Vec<_> = bodies
        .into_iter()
        .map(|(i, body)| {
            // we know we wrote u64's
            let v: u64 = 0;
            assert_eq!(body.len(), std::mem::size_of_val(&v), "unexpected size");
            (i, byteorder::NativeEndian::read_u64(&body[..]))
        })
        .collect();
    println!("bodies_decoded: {:#?}", bodies);

    bodies.iter().for_each(|(i, v)| match (i, v) {
        (0, 23) | (1, 42) => (),
        x => panic!("unexpected entry: {:?}", x),
    });

    unsafe {
        bindings::zilpmem_prb_promise_no_more_gc(prb.prb);
        bindings::zilpmem_prb_teardown_objset(hdl, false as boolean_t, std::ptr::null_mut());
    }
}

// enum WriteSequenceOp {
//     Write {
//         label: Option<&'static str>,
//         objset: u64,
//         txg: u64,
//         expect: Option<Box<dyn FnOnce(prb_write_result_t, prb_write_stats_t)>>,
//     },
//     WaitFor {
//         label: Option<&'static str>,
//         for_label: &'static str,
//     },
// }

// struct WriteSequenceTestDescription {
//     d: BTreeMap<&'static str, Vec<WriteSequenceOp>>,
// }

// // struct WriteSequenceTest {
// //     d: BTreeMap<&'static str, Vec<WriteSequenceOpState>>
// // }

// impl WriteSequenceTestDescription {
//     fn run(self) {

//     }
// }

// fn diagonale_2() {
//     let d = WriteSequenceTestDescription {
//         d: maplit::btreemap! {
//             "C1" => vec![
//                 WriteSequenceOp::Write{label: None, objset: 23, txg: 2, expect: None},
//                 WriteSequenceOp::Write{label: None, objset: 23, txg: 3, expect: None},
//                 WriteSequenceOp::Write{label: None, objset: 23, txg: 4, expect: None},
//                 WriteSequenceOp::WaitFor{label: Some("1"), for_label: "2"},
//                 WriteSequenceOp::Write{label: Some("3"), objset: 23, txg: 5, expect: None},
//             ],
//             "C2" => vec![
//                 WriteSequenceOp::Write{label: Some("2"), objset: 42, txg: 2, expect: None},
//                 WriteSequenceOp::WaitFor{label: None, for_label: "3"},
//                 WriteSequenceOp::Write{label: None, objset: 42, txg: 2,
//                     expect: Some(Box::new(|r, _| { assert_eq!(r, prb_write_result_t_PRB_WRITE_OBSOLETE); }))},
//             ]
//         },
//     };

//     d.run();
// }

#[test]
fn diagonale_write() {
    libzpool::init_once();

    let hcd = HeapChunkData::new_zeroed(1 << 20);
    let ch = alloc_heap_chunk(&hcd);

    let mut hdr = ZilHeaderPmem::init();

    let mut prb = Prb::new_empty(1);
    prb.add_chunk_for_write(&ch);

    let objset_id = 23;

    let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.prb, objset_id) };
    unsafe {
        bindings::zilpmem_prb_destroy_log(hdl, hdr.as_mut_ptr());
        let upd = bindings::zilpmem_prb_create_log_if_not_exists(hdl, hdr.as_mut_ptr());
        assert!(upd == boolean_t_B_TRUE);
    }

    for i in 2..30 {
        let body: u64 = i * 2;
        unsafe {
            let err = bindings::zilpmem_prb_write_entry_with_stats(
                hdl,
                i,
                true as bindings::boolean_t,
                std::mem::size_of_val(&body) as u64,
                &body as *const _ as *const c_void,
                false as boolean_t,
                std::ptr::null_mut(),
            );
            assert_eq!(err, 0);
        }
    }

    unsafe {
        bindings::zilpmem_prb_teardown_objset(hdl, true as boolean_t, hdr.as_mut_ptr());
    }
}

use crate::libzpool::zilpmem::WriteEntryMetadata;

use super::util::*;
use bindings::{
    check_replayable_result_kind_CHECK_REPLAYABLE_OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN,
    objset, prb_write_result_t_PRB_WRITE_OBSOLETE, prb_write_result_t_PRB_WRITE_OK,
    zil_header_pmem_state_ZHPM_ST_LOGGING,
};
use std::collections::HashSet;
use std::iter::FromIterator;
use zilpmem::ReplayCallbackArgCopied;

#[test]
fn write_v_shape() {
    libzpool::init_once();

    let cd = HeapChunkData::new_zeroed(1 << 20);
    let ch = alloc_heap_chunk(&cd);
    let mut hdr = ZilHeaderPmem::init();
    let objset_id = 0x1;

    let written_entries = {
        let mut prb = Prb::new_empty(1);
        prb.free_chunks = false;
        prb.add_chunk_for_write(&ch);

        let os = prb.setup_objset(objset_id, false);

        unsafe {
            bindings::zilpmem_prb_destroy_log(os.handle, hdr.as_mut_ptr());
            bindings::zilpmem_prb_create_log_if_not_exists(os.handle, hdr.as_mut_ptr());
        }

        let e1 = os.write_entry_mustnotblock(3, true, &[1]); // first gen
        let e2 = os.write_entry_mustnotblock(4, false, &[2]);
        let e3 = os.write_entry_mustnotblock(3, false, &[3]);

        drop(os);
        drop(prb);
        vec![e1, e2, e3]
    };

    let claim_txg = 1;

    // find all the entries
    let (replayed_entry_pmem_ptrs, replayed_entries) = {
        let mut prb = Prb::new_empty(1);
        prb.free_chunks = false;
        prb.add_chunk_for_claim(&ch); // !

        let os = prb.setup_objset(objset_id, false);

        // claim all entries with a mock claimstore, expect success
        let mut needs_claiming = |_rn| Ok(false);
        let mut claim = |_rn| {
            panic!("needs_claiming returns false => this should never be called");
        };
        let mut cs = MockClaimstore::new(&mut needs_claiming, &mut claim);
        let res = unsafe {
            let (csi, csi_arg) = cs.as_claimstore_interface();
            bindings::zilpmem_prb_claim(os.handle, hdr.as_mut_ptr(), claim_txg, csi, csi_arg)
        };
        assert_eq!(
            res.what,
            bindings::zilpmem_prb_claim_result_kind_PRB_CLAIM_RES_OK
        );

        // do replay that copies all entry headers
        let mut replayed_entry_pmem_ptrs = vec![];
        let mut replayed_entries = vec![];
        let res = zilpmem::replay_rust(os.handle, |arg| {
            let rn = &unsafe { *arg.node };
            replayed_entry_pmem_ptrs.push(rn.rn_pmem_ptr);
            replayed_entries.push(ReplayCallbackArgCopied::from(&arg));
            0
        });
        assert_eq!(
            res.what,
            bindings::zilpmem_prb_replay_result_kind_PRB_REPLAY_RES_OK
        );
        (replayed_entry_pmem_ptrs, replayed_entries)
    };

    // assert the pointers reported by write and by replay are the same
    {
        let written = written_entries
            .iter()
            .map(|wr| wr.entry_pmem_base as *const u8)
            .collect::<HashSet<_>>();
        let expect = replayed_entry_pmem_ptrs
            .iter()
            .cloned()
            .collect::<HashSet<_>>();
        assert_eq!(written, expect);
    }

    // assert exactly the written entries have been replayed
    {
        let written: HashSet<_> = written_entries
            .iter()
            .map(|wr| unsafe { wr.entry_pmem_base_as_entry_header_data() })
            .map(SynthEntry::from)
            .collect();
        let expect: HashSet<_> = replayed_entries
            .iter()
            .map(|rac| unsafe {
                // FIXME move this into a re-useable helper
                &(&*(rac.node.rn_pmem_ptr as *const bindings::entry_header_t)).eh_data
            })
            .map(SynthEntry::from)
            .collect();
        assert_eq!(written, expect);
    }

    // NOW IT GETS INTERESTING
    let expect = vec![
        SynthEntry(3, 1, 1, (0, [(0, 0), (0, 0), (0, 0)])),
        SynthEntry(4, 1, 2, (0, [(0, 0), (0, 0), (0, 0)])),
        SynthEntry(3, 1, 3, (0, [(0, 0), (0, 0), (0, 0)])),
    ];
    let expect = HashSet::from_iter(expect.into_iter());
    {
        let written: HashSet<_> = written_entries
            .iter()
            .map(|wr| unsafe { wr.entry_pmem_base_as_entry_header_data() })
            .map(SynthEntry::from)
            .collect();
        assert_eq!(expect, written);
    }
}

fn obsolete_test_wrapper<F>(f: F)
where
    F: FnOnce(&PrbHandle),
{
    libzpool::init_once();

    let cd = HeapChunkData::new_zeroed(1 << 20);
    let ch = alloc_heap_chunk(&cd);
    let mut hdr = ZilHeaderPmem::init();
    let objset_id = 0x1;

    let mut prb = Prb::new_empty(1);
    prb.free_chunks = false;
    prb.add_chunk_for_write(&ch);

    let os = prb.setup_objset(objset_id, false);

    unsafe {
        bindings::zilpmem_prb_destroy_log(os.handle, hdr.as_mut_ptr());
        bindings::zilpmem_prb_create_log_if_not_exists(os.handle, hdr.as_mut_ptr());
    }

    f(&os);

    drop(os);
    drop(prb);
}

macro_rules! checked_write {
    ($os:expr, $txg:expr, $newgen:expr, $expect:expr) => {{
        println!("  writing txg={}", $txg);
        let (_, res) = $os.write_entry($txg, $newgen, &[1], false);
        assert_eq!(res, $expect, "unexpected result");
    }};
}

#[test]
fn obsolete_entries_are_not_written() {
    for i in 1..=9 {
        /* txg=0 is not allowed */
        println!("running i={}", i);
        obsolete_test_wrapper(|os| {
            checked_write!(os, i, true /* first gen */, zilpmem::PRB_WRITE_OK);
            checked_write!(os, i + 1, false, zilpmem::PRB_WRITE_OK);
            checked_write!(os, i + 2, false, zilpmem::PRB_WRITE_OK);
            checked_write!(os, i + 3, false, zilpmem::PRB_WRITE_OK);
            // since TXG_CONCURRENT_STATES == 3 we know that txg i must have been synced because we wrote i + 3 before
            checked_write!(os, i, false, zilpmem::PRB_WRITE_OBSOLETE);
        });
    }
}

#[test]
fn writing_out_obsolete_entries_causes_replay_error() {
    libzpool::init_once();

    let cd = HeapChunkData::new_zeroed(1 << 20);
    let mut ch = alloc_heap_chunk(&cd);
    let mut hdr = ZilHeaderPmem::init();
    let objset_id = 0x1;

    // Set up a PRB with ZIL header in state 'logging'
    {
        let mut prb = Prb::new_empty(1);
        prb.free_chunks = false;
        prb.add_chunk_for_write(&ch);
        let os = prb.setup_objset(objset_id, false);
        unsafe {
            bindings::zilpmem_prb_destroy_log(os.handle, hdr.as_mut_ptr());
            bindings::zilpmem_prb_create_log_if_not_exists(os.handle, hdr.as_mut_ptr());
        }
        drop(os);
        drop(prb);
    }

    let entries = &[
        SynthEntry(5, 1, 1, (0, [(0, 0), (0, 0), (0, 0)])),
        SynthEntry(1, 1, 2, (0, [(0, 0), (0, 0), (0, 0)])),
    ];
    // write SynthEntries that a correct implementation wouldn't write
    {
        assert_eq!(
            hdr.as_impl().zhpm_st,
            zil_header_pmem_state_ZHPM_ST_LOGGING as u64
        );
        let zil_guid_1 = hdr.as_impl().zhpm_guid_1;
        let zil_guid_2 = hdr.as_impl().zhpm_guid_2;

        for e in entries {
            ch.write_entry_slow(
                &e.to_write_entry_md(objset_id, zil_guid_1, zil_guid_2),
                &[23],
            )
            .unwrap();
        }
    }

    // Re-open the PRB for claiming
    let mut prb = Prb::new_empty(1);
    prb.free_chunks = false;
    prb.add_chunk_for_claim(&ch);
    let os = prb.setup_objset(objset_id, false);

    let claim_txg = 1;
    // check that replay rejects it
    {
        // FIXME dedup this code with the write_v_shape() code above
        // or even better don't use the entire PRB + handle machinery at all and turn this into a replay test

        // claim all entries with a mock claimstore, expect success
        let mut needs_claiming = |_rn| Ok(false);
        let mut claim = |_rn| {
            panic!("needs_claiming returns false => this should never be called");
        };
        let mut cs = MockClaimstore::new(&mut needs_claiming, &mut claim);
        let res = unsafe {
            let (csi, csi_arg) = cs.as_claimstore_interface();
            bindings::zilpmem_prb_claim(os.handle, hdr.as_mut_ptr(), claim_txg, csi, csi_arg)
        };
        assert_eq!(
            res.what,
            bindings::zilpmem_prb_claim_result_kind_PRB_CLAIM_RES_ERR_STRUCTURAL
        );
        let structural = unsafe { res.__bindgen_anon_1.structural }; // checked above
        assert_eq!(structural.what, check_replayable_result_kind_CHECK_REPLAYABLE_OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN);
    }
}

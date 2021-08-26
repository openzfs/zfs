use crate::args_mmap::MmapArgs;
use bindings::{boolean_t, eh_dep_t, prb_write_stats, zil_header_pmem_impl_t};
use bindings::claimstore_interface_t;
use bindings::entry_header_data_t;
use bindings::zil_header_pmem_init;
use bindings::zil_header_pmem_t;
use bindings::zilpmem_prb_handle_t;
use bindings::zilpmem_prb_setup_objset;
use bindings::zilpmem_prb_t;
use bindings::zilpmem_prb_teardown_objset;
use bindings::zilpmem_prb_write_entry_with_stats;
use byteorder::ByteOrder;
use core::ffi::c_void;
use libc::c_int;
use std::{convert::TryInto, fmt::Write};

pub use crate::libzpool;
pub use libzpool::zfs_btree;
pub use libzpool::zilpmem;
pub use zilpmem::WriteStatsExt;
pub use zilpmem::ZilHeaderPmemPtrExt;

use self::zilpmem::ChunkPtr;

#[derive(Debug, PartialEq, Eq)]
pub struct HeapChunkData {
    pub ptr: *mut u8,
    pub layout: std::alloc::Layout,
    pub chunklen: usize,
}

impl HeapChunkData {
    pub fn new_zeroed(chunklen: usize) -> Self {
        let layout = std::alloc::Layout::from_size_align(chunklen, 256).unwrap();
        let ptr = unsafe { std::alloc::alloc_zeroed(layout) };
        HeapChunkData {
            ptr,
            layout,
            chunklen,
        }
    }
}

impl Drop for HeapChunkData {
    fn drop(&mut self) {
        unsafe { std::alloc::dealloc(self.ptr, self.layout) };
    }
}

pub fn alloc_heap_chunk(data: &HeapChunkData) -> zilpmem::ChunkPtr {
    zilpmem::ChunkPtr::from_raw(
        unsafe { bindings::prb_chunk_alloc(data.ptr, data.chunklen as u64) }
            as *mut zilpmem::prb_chunk_t,
    )
}

impl HeapChunkData {
    fn init_chunk_iter(&self) -> bindings::prb_chunk_iter_t {
        let mut w = std::mem::MaybeUninit::uninit();
        unsafe {
            bindings::prb_chunk_iter_init(self.ptr, self.chunklen as u64, w.as_mut_ptr());
            w.assume_init()
        }
    }

    /// XXX rust enum that hides the OK chunk_iter_result_t
    pub fn iter(&self) -> impl Iterator<Item = Result<*const u8, bindings::prb_chunk_iter_result_t>> {
        let mut w = self.init_chunk_iter();
        std::iter::from_fn(move || unsafe {
            let mut entry = std::mem::MaybeUninit::uninit();
            let res = bindings::prb_chunk_iter(&mut w, entry.as_mut_ptr());
            match res {
                bindings::prb_chunk_iter_result_PRB_CHUNK_ITER_OK => {
                    let entry: *const u8 = entry.assume_init();
                    if entry == std::ptr::null() {
                        None
                    } else {
                        Some(Ok(entry))
                    }
                }
                x => Some(Err(x)),
            }
        })
    }
}

pub struct ZilHeaderPmem {
    hdr: zil_header_pmem_t,
}

impl std::ops::Deref for ZilHeaderPmem {
    type Target = zil_header_pmem_t;
    fn deref(&self) -> &Self::Target {
        &self.hdr
    }
}

impl std::ops::DerefMut for ZilHeaderPmem {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.hdr
    }
}

impl ZilHeaderPmem {
    pub fn init() -> Self {
        let mut hdr = std::mem::MaybeUninit::uninit();
        let hdr = unsafe {
            zil_header_pmem_init(hdr.as_mut_ptr());
            hdr.assume_init()
        };
        Self { hdr }
    }
    pub fn as_mut_ptr(&mut self) -> *mut zil_header_pmem_t {
        &mut self.hdr
    }
    pub fn as_impl(&self) -> &zil_header_pmem_impl_t {
        assert_eq!(std::mem::size_of_val(self), std::mem::size_of::<zil_header_pmem_impl_t>());
        unsafe { std::mem::transmute(self) } 
    }
}

pub struct Prb {
    pub prb: *mut zilpmem::prb_t,
    pub free_chunks: bool,
}

impl Prb {
    pub fn new_empty(ncommitters: usize) -> Self {
        let prb = unsafe { zilpmem::prb_alloc(ncommitters as u64) };
        Prb {
            prb,
            free_chunks: true,
        }
    }

    pub fn new_anon(len: usize, ncommitters: usize) -> Self {
        let prb = Self::new_empty(ncommitters);
        const CHUNKSIZE: usize = 1 << 27;
        let mut mmap = MmapArgs {
            size: len,
            path: None,
        }
        .mmap()
        .unwrap();
        let mmap = zilpmem::BaseChunklenNumchunks::from_into_base_len(&mut mmap, CHUNKSIZE);
        mmap.add_to_prb_for_write(prb.prb);
        prb
    }

    pub fn new_anon_with_chunksize(nchunks: usize, chunksize: usize, ncommitters: usize) -> Self {
        let prb = Self::new_empty(ncommitters);
        let mut mmap = MmapArgs {
            size: nchunks * chunksize,
            path: None,
        }
        .mmap()
        .unwrap();
        let mmap = zilpmem::BaseChunklenNumchunks::from_into_base_len(&mut mmap, chunksize);
        mmap.add_to_prb_for_write(prb.prb);
        prb
    }

    pub fn add_chunk_for_write(&mut self, p: &zilpmem::ChunkPtr) {
        unsafe { bindings::zilpmem_prb_add_chunk_for_write(self.prb, p.0) }
    }

    pub fn add_chunk_for_claim(&mut self, p: &zilpmem::ChunkPtr) {
        unsafe { bindings::zilpmem_prb_add_chunk_for_claim(self.prb, p.0) }
    }

    pub fn setup_objset<'a>(
        &'a mut self,
        objset_id: u64,
        abandon_claim_on_drop: bool,
    ) -> PrbHandle<'a> {
        let handle = unsafe { zilpmem_prb_setup_objset(self.prb, objset_id) };
        PrbHandle {
            handle,
            _prb: self,
            abandon_claim_on_drop,
        }
    }

    pub fn gc(&mut self) {
        unimplemented!()
    }
}

impl Drop for Prb {
    fn drop(&mut self) {
        unsafe {
            zilpmem::prb_free(self.prb, self.free_chunks as boolean_t);
        }
    }
}

pub struct PrbHandle<'prb> {
    pub handle: *mut zilpmem_prb_handle_t,
    _prb: &'prb Prb, // acts as marker
    abandon_claim_on_drop: bool,
}

pub fn make_zil_guid() -> (u64, u64) {
    let u = uuid::Uuid::new_v4();
    let u = u.as_bytes();
    (
        byteorder::NativeEndian::read_u64(&u[0..8]),
        byteorder::NativeEndian::read_u64(&u[8..16]),
    )
}

impl<'prb> PrbHandle<'prb> {
    pub fn write_entry_mustnotblock(
        &self,
        txg: u64,
        needs_new_gen: bool,
        body: &[u8],
    ) -> zilpmem::prb_write_stats_t {
        let mut stats = std::mem::MaybeUninit::uninit();
        let res = unsafe {
            zilpmem_prb_write_entry_with_stats(
                self.handle,
                txg,
                needs_new_gen as boolean_t,
                body.len().try_into().unwrap(),
                body.as_ptr() as *const c_void,
                false as u32,
                stats.as_mut_ptr(),
            )
        };
        assert_eq!(res, bindings::prb_write_result_t_PRB_WRITE_OK);
        let st = unsafe { stats.assume_init() };
        assert_ne!(st.entry_pmem_base, std::ptr::null_mut());
        st
    }

    pub fn write_entry(
        &self,
        txg: u64,
        needs_new_gen: bool,
        body: &[u8],
        may_sleep: bool,
    ) -> (bindings::prb_write_stats_t, bindings::prb_write_result_t) {
        unsafe {
            let mut stats = std::mem::MaybeUninit::uninit();
            let res = zilpmem_prb_write_entry_with_stats(
                self.handle,
                txg,
                needs_new_gen as boolean_t,
                body.len().try_into().unwrap(),
                body.as_ptr() as *const c_void,
                may_sleep as u32,
                stats.as_mut_ptr(),
            );
            (stats.assume_init(), res)
        }
    }
}

impl<'prb> Drop for PrbHandle<'prb> {
    fn drop(&mut self) {
        unsafe {
            if self.abandon_claim_on_drop {
                zilpmem_prb_teardown_objset(self.handle, false as boolean_t, std::ptr::null_mut())
            } else {
                let mut zh = ZilHeaderPmem::init();
                zilpmem_prb_teardown_objset(self.handle, true as boolean_t, zh.as_mut_ptr())
            }
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash)]
pub struct SynthEntry(pub u64, pub u64, pub u64, pub (u64, [(u64, u64); 3]));

impl SynthEntry {
    fn to_eh_dep(&self) -> eh_dep_t {
        let get_pair = |idx| {
            let last_gen_counts_shortform = (self.3).1;
            let shortform: (_, _) = last_gen_counts_shortform[idx];
            bindings::prb_deptrack_count_pair_t {
                dtp_txg: shortform.0,
                dtp_count: shortform.1,
            }
        };
        bindings::eh_dep_t {
            eh_last_gen: (self.3).0,
            eh_last_gen_counts: [get_pair(0), get_pair(1), get_pair(2)],
        }
    }
    pub fn to_write_entry_md(&self, objset_id: u64, zil_guid_1: u64, zil_guid_2: u64) -> zilpmem::WriteEntryMetadata {
        zilpmem::WriteEntryMetadata {
            objset_id,
            zil_guid_1,
            zil_guid_2,
            txg: self.0,
            gen: self.1,
            id: self.2,
            dep: self.to_eh_dep(),
        }
    }
}

impl Into<zilpmem::replay_node_t> for SynthEntry {
    fn into(self) -> zilpmem::replay_node_t {
        let SynthEntry(txg, gen, id, (_eh_last_gen, _last_gen_counts_shortform)) = self;
        zilpmem::replay_node_t {
            rn_gen: gen,
            rn_id: id,
            rn_pmem_ptr: std::ptr::null(),
            rn_chunk: std::ptr::null_mut(),
            rn_txg: txg,
            rn_dep: self.to_eh_dep(),
        }
    }
}

impl<'a> From<&'a zilpmem::entry_header_data_t> for SynthEntry {
    fn from(o: &'a zilpmem::entry_header_data_t) -> SynthEntry {
        let &zilpmem::entry_header_data_t {
            eh_gen,
            eh_txg,
            eh_gen_scoped_id,
            eh_dep,
            ..
        } = o;
        let mut last_gen_counts_shortform = [(0, 0); 3];
        assert_eq!(
            last_gen_counts_shortform.len(),
            eh_dep.eh_last_gen_counts.len()
        );
        for (i, pair) in eh_dep.eh_last_gen_counts.iter().enumerate() {
            last_gen_counts_shortform[i] = (pair.dtp_txg, pair.dtp_count);
        }
        SynthEntry(
            eh_txg,
            eh_gen,
            eh_gen_scoped_id,
            (eh_dep.eh_last_gen, last_gen_counts_shortform),
        )
    }
}


type NeedsClaimingCbRef<'r> =
    &'r mut dyn FnMut(*const zilpmem::replay_node_t) -> Result<bool, c_int>;
type ClaimCbRef<'r> = &'r mut dyn FnMut(*const zilpmem::replay_node_t) -> Result<(), c_int>;
pub struct MockClaimstore<'capture> {
    needs_claiming: NeedsClaimingCbRef<'capture>,
    claim: ClaimCbRef<'capture>,
    claimstore_interface: claimstore_interface_t,
}

extern "C" fn mock_claimstore_needs_store_claim(
    arg: *mut c_void,
    rn: *const zilpmem::replay_node_t,
    needs_claiming: *mut bindings::boolean_t,
) -> c_int {
    let res = std::panic::catch_unwind(|| {
        let cs = unsafe { &mut *(arg as *mut MockClaimstore) };
        let res = (cs.needs_claiming)(rn);
        match res {
            Err(e) => {
                assert_ne!(e, 0);
                return e;
            }
            Ok(nc) => {
                unsafe { *needs_claiming = nc as bindings::boolean_t };
                return 0;
            }
        }
    });
    match res {
        Ok(r) => return r,
        Err(_cause) => {
            eprintln!("mock claimstore needs_claiming() panicked");
            std::process::abort();
        }
    }
}

extern "C" fn mock_claimstore_claim(arg: *mut c_void, rn: *const zilpmem::replay_node_t) -> c_int {
    let res = std::panic::catch_unwind(|| {
        let cs = unsafe { &mut *(arg as *mut MockClaimstore) };
        match (cs.claim)(rn) {
            Err(e) => {
                assert_ne!(e, 0);
                return e;
            }
            Ok(()) => {
                return 0;
            }
        }
    });
    match res {
        Ok(r) => return r,
        Err(_cause) => {
            eprintln!("mock claimstore claim() panicked");
            std::process::abort();
        }
    }
}

impl<'capture> MockClaimstore<'capture> {
    pub fn new(needs_claiming: NeedsClaimingCbRef<'capture>, claim: ClaimCbRef<'capture>) -> Self {
        MockClaimstore {
            needs_claiming,
            claim,
            claimstore_interface: claimstore_interface_t {
                prbcsi_needs_store_claim: Some(mock_claimstore_needs_store_claim),
                prbcsi_claim: Some(mock_claimstore_claim),
            },
        }
    }
    pub fn as_claimstore_interface(&mut self) -> (*const claimstore_interface_t, *mut c_void) {
        (&self.claimstore_interface, self as *mut _ as *mut c_void)
    }
}

type MockReplayCb<'c> =
    &'c mut dyn FnMut(*const zilpmem::replay_node_t, *const zil_header_pmem_t) -> c_int;

pub struct MockReplay<'c> {
    callback: MockReplayCb<'c>,
}

impl<'c> MockReplay<'c> {
    pub fn new(callback: MockReplayCb<'c>) -> Self {
        Self { callback }
    }
    pub fn as_arg(&mut self) -> (bindings::zilpmem_replay_cb_t, *mut c_void) {
        extern "C" fn cb(
            arg: *mut c_void,
            rn: *const zilpmem::replay_node_t,
            upd: *const zil_header_pmem_t,
        ) -> c_int {
            let res = std::panic::catch_unwind(|| {
                let mr = unsafe { &mut *(arg as *mut MockReplay) };
                (mr.callback)(rn, upd)
            });
            match res {
                Ok(r) => return r,
                Err(_cause) => {
                    eprintln!("mock replay callback panicked");
                    std::process::abort();
                }
            }
        }

        (Some(cb), self as *mut _ as *mut c_void)
    }
}

#[derive(Debug)]
pub struct ReplayNodeReading {
    pub header: entry_header_data_t,
    pub body: bytes::Bytes,
}

pub unsafe fn read_replay_node(
    rn: *const zilpmem::replay_node_t,
) -> Result<ReplayNodeReading, bindings::zilpmem_prb_replay_read_replay_node_result_t> {
    // read the header
    let mut eh = std::mem::MaybeUninit::<bindings::entry_header_t>::uninit();
    let mut required_size = std::mem::MaybeUninit::uninit();
    let res = bindings::zilpmem_prb_replay_read_replay_node(
        rn,
        eh.as_mut_ptr(),
        std::ptr::null_mut(),
        0,
        required_size.as_mut_ptr(),
    );

    if res == bindings::zilpmem_prb_replay_read_replay_node_result_t_READ_REPLAY_NODE_OK {
        return Ok(ReplayNodeReading {
            header: eh.assume_init().eh_data,
            body: bytes::Bytes::new(),
        });
    }

    if res != bindings::zilpmem_prb_replay_read_replay_node_result_t_READ_REPLAY_NODE_ERR_BODY_SIZE_TOO_SMALL {
        return Err(res);
    }

    // Read again, this time with a body

    let required_size = required_size.assume_init(); // safe because res != READ_REPLAY_NODE_ERR_BODY_SIZE_TOO_SMALL
    let mut body = bytes::BytesMut::new();
    body.resize(required_size.try_into().unwrap(), 0);

    let mut required_size2 = std::mem::MaybeUninit::uninit();
    let res = bindings::zilpmem_prb_replay_read_replay_node(
        rn,
        eh.as_mut_ptr(),
        body.as_mut_ptr(),
        body.len() as u64,
        required_size2.as_mut_ptr(),
    );

    match res {
        bindings::zilpmem_prb_replay_read_replay_node_result_t_READ_REPLAY_NODE_OK => {
            let required_size2 = required_size2.assume_init(); // safe because res != READ_REPLAY_NODE_ERR_BODY_SIZE_TOO_SMALL
            assert_eq!(
                required_size, required_size2,
                "should not change between readings"
            );

            Ok(ReplayNodeReading {
                header: eh.assume_init().eh_data,
                body: body.into(),
            })
        }
        // note: reuqired_size2 is not necessarily initialized because we have not checked READ_REPLAY_NODE_ERR_BODY_SIZE_TOO_SMALL
        x => Err(x),
    }
}

use bindings::{prb_chunk_alloc, zil_header_pmem_t};
use bindings::{prb_write_chunk, zil_header_pmem_impl_t};
use core::ffi::c_void;
use derivative::Derivative;
use libc::c_int;
use std::fmt;
use std::{convert::{TryInto,TryFrom}, mem::MaybeUninit};

pub use bindings::entry_header_data_t;
pub use bindings::prb_chunk_t;
pub use bindings::prb_write_stats_t;
pub use bindings::zilpmem_prb as prb_t;
pub use bindings::zilpmem_prb_all_chunks as prb_all_chunks;
pub use bindings::zilpmem_prb_alloc as prb_alloc;
pub use bindings::zilpmem_prb_free as prb_free;
pub use bindings::zilpmem_prb_gc as prb_gc;
pub use bindings::zilpmem_prb_handle_t as prb_handle_t;
pub use bindings::zilpmem_prb_write_entry as prb_write_entry;
pub use bindings::zilpmem_replay_cb_t as replay_cb_t;
pub use bindings::zilpmem_replay_node as replay_node_t;
pub use bindings::zilpmem_replay_resume as replay_resume;
pub use bindings::zilpmem_replay_resume_cb_result_t as replay_resume_cb_result_t;
pub use bindings::zilpmem_replay_state as replay_state_t;
pub use bindings::zilpmem_replay_state_init as replay_state_init;

pub use bindings::prb_write_result_t_PRB_WRITE_EWOULDSLEEP as PRB_WRITE_EWOULDSLEEP;
pub use bindings::prb_write_result_t_PRB_WRITE_OBSOLETE as PRB_WRITE_OBSOLETE;
pub use bindings::prb_write_result_t_PRB_WRITE_OK as PRB_WRITE_OK;

pub trait IntoBaseLen {
    fn into_base_len(&mut self) -> (*mut u8, u64);
}

impl<'a> IntoBaseLen for &'a mut [u8] {
    fn into_base_len(&mut self) -> (*mut u8, u64) {
        (self.as_mut_ptr(), self.len().try_into().unwrap())
    }
}

pub struct BaseChunklenNumchunks {
    pub base: *mut u8,
    pub chunklen: u64,
    pub numchunks: u64,
}

impl BaseChunklenNumchunks {
    pub fn from_into_base_len<B: IntoBaseLen>(o: &mut B, chunklen: usize) -> Self {
        let (base, len) = o.into_base_len();
        let numchunks = len / (chunklen as u64);
        BaseChunklenNumchunks {
            base,
            chunklen: chunklen as u64,
            numchunks,
        }
    }

    fn add_to_prb_for(&self, prb: *mut prb_t, add_chunk_fn: unsafe extern "C" fn(*mut prb_t, *mut prb_chunk_t)) {
        let mut offset = 0;
        for nchunk in (0..self.numchunks) {
            let ch = unsafe {
                prb_chunk_alloc(self.base.offset(isize::try_from(self.chunklen * nchunk).unwrap()), self.chunklen)
            };
            unsafe { add_chunk_fn(prb, ch) };
        }
    }

    pub fn add_to_prb_for_write(&self, prb: *mut prb_t) {
        self.add_to_prb_for(prb, bindings::zilpmem_prb_add_chunk_for_write)
    }

    pub fn add_to_prb_for_claim(&self, prb: *mut prb_t) {
        self.add_to_prb_for(prb, bindings::zilpmem_prb_add_chunk_for_claim)
    }
}

impl<T> Into<BaseChunklenNumchunks> for (T, u64)
where
    T: IntoBaseLen,
{
    fn into(mut self) -> BaseChunklenNumchunks {
        BaseChunklenNumchunks::from_into_base_len(&mut self.0, self.1.try_into().unwrap())
    }
}

#[derive(Clone, Copy)]
pub struct ReplayResumeCallbackArg {
    pub node: *const replay_node_t,
    pub state: *const replay_state_t,
}
#[derive(Debug, Clone)]
pub struct ReplayResumeCallbackArgCopied {
    pub node: replay_node_t,
    pub state: replay_state_t,
}

impl<'a> From<&'a ReplayResumeCallbackArg> for ReplayResumeCallbackArgCopied {
    fn from(o: &'a ReplayResumeCallbackArg) -> Self {
        Self {
            node: unsafe { *o.node },
            state: unsafe { *o.state },
        }
    }
}

impl fmt::Debug for ReplayResumeCallbackArg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&ReplayResumeCallbackArgCopied::from(self), f)
    }
}

#[must_use]
pub fn replay_resume_rust<F>(
    bt: *mut bindings::zfs_btree_t,
    w: *mut bindings::zfs_btree_index_t,
    st: &mut replay_state_t,
    mut cb: F,
) -> bindings::check_replayable_result_t
where
    F: FnMut(ReplayResumeCallbackArg) -> replay_resume_cb_result_t,
{
    // https://stackoverflow.com/questions/32270030/how-do-i-convert-a-rust-closure-to-a-c-style-callback
    let mut cb: &mut dyn FnMut(ReplayResumeCallbackArg) -> replay_resume_cb_result_t = &mut cb; // trait object
    let cb = &mut cb; // pointer to trait object

    extern "C" fn cb_fn(
        arg: *mut c_void,
        node: *const replay_node_t,
        state: *const replay_state_t,
    ) -> replay_resume_cb_result_t {
        let cb: &mut &mut dyn FnMut(ReplayResumeCallbackArg) -> replay_resume_cb_result_t =
            unsafe { std::mem::transmute(arg) };
        cb(ReplayResumeCallbackArg { node, state })
    }

    unsafe { replay_resume(bt, w, st, Some(cb_fn), cb as *mut _ as *mut c_void) }
}

#[derive(Clone, Copy)]
pub struct ReplayCallbackArg {
    pub node: *const replay_node_t,
    pub upd: *const zil_header_pmem_impl_t,
}
#[derive(Debug, Clone)]
pub struct ReplayCallbackArgCopied {
    pub node: replay_node_t,
    pub upd: zil_header_pmem_impl_t,
}

impl<'a> From<&'a ReplayCallbackArg> for ReplayCallbackArgCopied {
    fn from(o: &'a ReplayCallbackArg) -> Self {
        Self {
            node: unsafe { *o.node },
            upd: unsafe { *o.upd },
        }
    }
}

impl fmt::Debug for ReplayCallbackArg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&ReplayCallbackArgCopied::from(self), f)
    }
}

#[must_use]
pub fn replay_rust<F>(
    hdl: *mut bindings::zilpmem_prb_handle_t,
    mut cb: F,
) -> bindings::zilpmem_prb_replay_result_t
where
    F: FnMut(ReplayCallbackArg) -> c_int,
{
    // https://stackoverflow.com/questions/32270030/how-do-i-convert-a-rust-closure-to-a-c-style-callback
    let mut cb: &mut dyn FnMut(ReplayCallbackArg) -> c_int = &mut cb; // trait object
    let cb = &mut cb; // pointer to trait object

    extern "C" fn cb_fn(
        arg: *mut c_void,
        node: *const replay_node_t,
        upd: *const zil_header_pmem_t,
    ) -> c_int {
        let cb: &mut &mut dyn FnMut(ReplayCallbackArg) -> c_int =
            unsafe { std::mem::transmute(arg) };
        let upd = upd as *const _ as *const zil_header_pmem_impl_t;
        cb(ReplayCallbackArg { node, upd })
    }

    unsafe { bindings::zilpmem_prb_replay(hdl, Some(cb_fn), cb as *mut _ as *mut c_void) }
}

pub trait ZilHeaderPmemPtrExt {
    fn as_zh_pmem_impl_ptr(self) -> *const zil_header_pmem_impl_t;
}

impl ZilHeaderPmemPtrExt for *const zil_header_pmem_t {
    fn as_zh_pmem_impl_ptr(self) -> *const zil_header_pmem_impl_t {
        let upd = self as *const _ as *const zil_header_pmem_impl_t;
        upd
    }
}

pub struct WriteEntryMetadata {
    pub objset_id: u64,
    pub zil_guid_1: u64,
    pub zil_guid_2: u64,
    pub txg: u64,
    pub gen: u64,
    pub id: u64,
    pub dep: bindings::eh_dep_t,
}

#[derive(Debug, Derivative, Clone, Copy, PartialEq)]
pub struct ChunkPtr(pub *mut prb_chunk_t);

impl ChunkPtr {
    pub fn ch_base(&self) -> *mut u8 {
        unsafe { &*self.0 }.ch_base
    }
    pub fn contains(&self, ptr: *mut u8) -> bool {
        unsafe { bindings::prb_chunk_contains_ptr(self.0, ptr) == 1 }
    }
    pub fn from_raw(p: *mut prb_chunk_t) -> Self {
        assert_ne!(p, std::ptr::null_mut());
        ChunkPtr(p)
    }
    pub fn into_raw(self) -> *mut prb_chunk_t {
        self.0
    }
    pub fn write_entry_slow(
        &mut self,
        md: &WriteEntryMetadata,
        body: &[u8],
    ) -> Result<prb_write_stats_t, ()> {
        let WriteEntryMetadata {
            objset_id,
            zil_guid_1,
            zil_guid_2,
            txg,
            gen,
            id,
            dep,
        } = md;
        unsafe {
            let mut staging_header = MaybeUninit::uninit();
            let mut staging_last_256b_block = vec![0 as u8; 256];
            let mut stats = MaybeUninit::uninit();
            let res = bindings::prb_write_chunk(
                self.0,
                *objset_id,
                *zil_guid_1,
                *zil_guid_2,
                *txg,
                *gen,
                *id,
                *dep,
                body.as_ptr(),
                body.len() as u64,
                staging_header.as_mut_ptr(),
                staging_last_256b_block.as_mut_ptr(),
                stats.as_mut_ptr(),
            );
            match res {
                bindings::prb_write_raw_chunk_result_t_WRITE_CHUNK_OK => Ok(stats.assume_init()),
                bindings::prb_write_raw_chunk_result_t_WRITE_CHUNK_ENOSPACE => Err(()),
                x => panic!("unexpected variant: {:?}", x),
            }
        }

    }
}

impl PartialEq<*mut prb_chunk_t> for ChunkPtr {
    fn eq(&self, o: &*mut prb_chunk_t) -> bool {
        self.0 == *o
    }
}

pub trait WriteStatsExt {
    fn entry_chunk_ptr(self) -> ChunkPtr;
    unsafe fn entry_pmem_base_as_entry_header_data(&self) -> &bindings::entry_header_data_t;
    fn slept(&self) -> bool;
}

impl WriteStatsExt for bindings::prb_write_stats_t {
    fn entry_chunk_ptr(self) -> ChunkPtr {
        ChunkPtr::from_raw(self.entry_chunk)
    }
    fn slept(&self) -> bool {
        self.get_chunk_calls_sleeps > 0
    }
    unsafe fn entry_pmem_base_as_entry_header_data(&self) -> &bindings::entry_header_data_t {
        let eh = self.entry_pmem_base as *const bindings::entry_header_t;
        &(*eh).eh_data
    }
}

use crate::base_types::*;
use crate::block_access::*;
use crate::block_allocator::zcachedb_dump_spacemaps;
use crate::block_allocator::BlockAllocator;
use crate::block_allocator::BlockAllocatorPhys;
use crate::block_based_log::*;
use crate::extent_allocator::ExtentAllocator;
use crate::extent_allocator::ExtentAllocatorPhys;
use crate::index::*;
use crate::DumpStructuresOptions;
use anyhow::Result;
use conv::ConvUtil;
use futures::future;
use futures::stream::*;
use futures::Future;
use lazy_static::lazy_static;
use log::*;
use metered::common::*;
use metered::hdr_histogram::AtomicHdrHistogram;
use metered::metered;
use metered::time_source::StdInstantMicros;
use more_asserts::*;
use serde::{Deserialize, Serialize};
use std::collections::btree_map;
use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::ops::Bound::{Excluded, Unbounded};
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;
use std::time::Instant;
use tokio::sync::OwnedSemaphorePermit;
use tokio::sync::Semaphore;
use tokio::time::{sleep_until, timeout_at};
use util::get_tunable;
use util::maybe_die_with;
use util::From64;
use util::LockSet;
use util::LockedItem;
use util::MutexExt;

lazy_static! {
    static ref SUPERBLOCK_SIZE: u64 = get_tunable("superblock_size", 4 * 1024);
    static ref DEFAULT_CHECKPOINT_RING_BUFFER_SIZE: u32 = get_tunable("default_checkpoint_ring_buffer_size", 1024 * 1024);
    pub static ref DEFAULT_SLAB_SIZE: u32 = get_tunable("default_slab_size", 16 * 1024 * 1024);
    static ref DEFAULT_METADATA_SIZE_PCT: f64 = get_tunable("default_metadata_size_pct", 15.0); // Can lower this to test forced eviction.
    static ref MAX_PENDING_CHANGES: usize = get_tunable("max_pending_changes", 50_000); // XXX should be based on RAM usage, ~tens of millions at least
    static ref CHECKPOINT_INTERVAL: Duration = Duration::from_secs(get_tunable("checkpoint_interval_secs", 60));
    static ref MERGE_PROGRESS_MESSAGE_INTERVAL: Duration = Duration::from_millis(get_tunable("merge_progress_message_interval_ms", 1000));
    static ref MERGE_PROGRESS_CHECK_COUNT: u32 = get_tunable("merge_progress_check_count", 100);
    static ref TARGET_CACHE_SIZE_PCT: u64 = get_tunable("target_cache_size_pct", 80);
    static ref HIGH_WATER_CACHE_SIZE_PCT: u64 = get_tunable("high_water_cache_size_pct", 82);

    static ref CACHE_INSERT_BLOCKING_BUFFER_BYTES: usize = get_tunable("cache_insert_blocking_buffer_bytes", 256_000_000);
    static ref CACHE_INSERT_NONBLOCKING_BUFFER_BYTES: usize = get_tunable("cache_insert_nonblocking_buffer_bytes", 256_000_000);
}

#[derive(Serialize, Deserialize, Debug)]
struct ZettaSuperBlockPhys {
    checkpoint_ring_buffer_size: u32,
    slab_size: u32,
    last_checkpoint_id: CheckpointId,
    last_checkpoint_extent: Extent,
    // XXX put sector size in here too and verify it matches what the disk says now?
    // XXX put disk size in here so we can detect expansion?
}

impl ZettaSuperBlockPhys {
    // XXX when we have multiple disks, will this be stored on a specific one?  Or copied on all of them?
    async fn read(block_access: &BlockAccess) -> Result<ZettaSuperBlockPhys> {
        let raw = block_access
            .read_raw(Extent {
                location: DiskLocation { offset: 0 },
                size: *SUPERBLOCK_SIZE,
            })
            .await;
        let (this, _): (Self, usize) = block_access.chunk_from_raw(&raw)?;
        debug!("got {:#?}", this);
        Ok(this)
    }

    async fn write(&self, block_access: &BlockAccess) {
        maybe_die_with(|| format!("before writing {:#?}", self));
        debug!("writing {:#?}", self);
        let raw = block_access.chunk_to_raw(EncodeType::Json, self);
        block_access
            .write_raw(DiskLocation { offset: 0 }, raw)
            .await;
    }
}

#[derive(Serialize, Deserialize, Debug)]
struct ZettaCheckpointPhys {
    generation: CheckpointId,
    extent_allocator: ExtentAllocatorPhys,
    block_allocator: BlockAllocatorPhys,
    last_atime: Atime,
    index: ZettaCacheIndexPhys,
    operation_log: BlockBasedLogPhys<OperationLogEntry>,
    merge_progress: Option<(BlockBasedLogPhys<OperationLogEntry>, ZettaCacheIndexPhys)>,
}

impl ZettaCheckpointPhys {
    async fn read(block_access: &BlockAccess, extent: Extent) -> ZettaCheckpointPhys {
        let raw = block_access.read_raw(extent).await;
        let (this, _): (Self, usize) = block_access.chunk_from_raw(&raw).unwrap();
        debug!("got {:#?}", this);
        this
    }

    /*
    fn all_logs(&self) -> Vec<&BlockBasedLogPhys> {
        vec![&self.index, &self.chunk_summary, &self.operation_log]
    }
    */
}

#[derive(Debug, Clone, Copy)]
enum PendingChange {
    Insert(IndexValue),
    UpdateAtime(IndexValue),
    Remove(),
    RemoveThenInsert(IndexValue),
}

#[derive(Clone)]
pub struct ZettaCache {
    block_access: Arc<BlockAccess>,
    // lock ordering: index first then state
    index: Arc<tokio::sync::RwLock<ZettaCacheIndex>>,
    // XXX may need to break up this big lock.  At least we aren't holding it while doing i/o
    state: Arc<tokio::sync::Mutex<ZettaCacheState>>,
    outstanding_lookups: LockSet<IndexKey>,
    metrics: Arc<ZettaCacheMetrics>,
    blocking_buffer_bytes_available: Arc<Semaphore>,
    nonblocking_buffer_bytes_available: Arc<Semaphore>,
}

#[derive(Debug, Serialize, Deserialize, Copy, Clone)]
struct ChunkSummaryEntry {
    offset: LogOffset,
    first_key: IndexKey,
}
impl OnDisk for ChunkSummaryEntry {}
impl BlockBasedLogEntry for ChunkSummaryEntry {}

#[derive(Debug, Serialize, Deserialize, Copy, Clone)]
enum OperationLogEntry {
    Insert(IndexKey, IndexValue),
    Remove(IndexKey, IndexValue),
}
impl OnDisk for OperationLogEntry {}
impl BlockBasedLogEntry for OperationLogEntry {}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
pub struct AtimeHistogramPhys {
    histogram: Vec<u64>,
    first: Atime,
}

impl AtimeHistogramPhys {
    pub fn new(first: Atime) -> AtimeHistogramPhys {
        AtimeHistogramPhys {
            histogram: Default::default(),
            first,
        }
    }

    pub fn first(&self) -> Atime {
        self.first
    }

    /// Reset the start to a later atime, discarding older entries.
    /// Requests to reset to an earlier atime are ignored.
    pub fn reset_first(&mut self, new_first: Atime) {
        if new_first <= self.first {
            return;
        }
        let delta = new_first - self.first;
        // XXX - if this becomes a bottleneck we should change the histogram to a VecDeque
        // so that we don't have to copy when deleting the head of the histogram
        self.histogram.drain(0..delta);
        self.first = new_first;
    }

    pub fn atime_for_target_size(&self, target_size: u64) -> Atime {
        info!(
            "histogram starts at {:?} and has {} entries",
            self.first,
            self.histogram.len()
        );
        let mut remaining = target_size;
        for (index, &bytes) in self.histogram.iter().enumerate().rev() {
            if remaining <= bytes {
                trace!("final include of {} for target at bucket {}", bytes, index);
                return Atime(self.first.0 + index as u64);
            }
            trace!("including {} in target at bucket {}", bytes, index);
            remaining -= bytes;
        }
        self.first
    }

    pub fn insert(&mut self, value: IndexValue) {
        assert_ge!(value.atime, self.first);
        let index = value.atime - self.first;
        if index >= self.histogram.len() {
            self.histogram.resize(index + 1, 0);
        }
        self.histogram[index] += u64::from(value.size);
    }

    pub fn remove(&mut self, value: IndexValue) {
        assert_ge!(value.atime, self.first);
        let index = value.atime - self.first;
        self.histogram[index] -= u64::from(value.size);
    }

    pub fn clear(&mut self) {
        self.histogram.clear();
    }

    fn sum(&self) -> u64 {
        self.histogram.iter().sum()
    }
}

#[derive(Debug, Serialize, Deserialize)]
struct MergeProgress {
    new_index: ZettaCacheIndexPhys,
    free_list: Vec<IndexValue>,
}

// #[derive(Serialize, Deserialize)]
#[derive(Debug)]
enum MergeMessage {
    Progress(MergeProgress),
    Complete(ZettaCacheIndex),
}

impl MergeMessage {
    /// Compose a progress update to send to the checkpoint task.
    async fn new_progress(next_index: &mut ZettaCacheIndex, free_list: Vec<IndexValue>) -> Self {
        let timer = Instant::now();
        let free_count = free_list.len();
        let message = MergeProgress {
            new_index: next_index.flush().await,
            free_list,
        };
        trace!("sending progress: index with {} entries ({}MB) last is {:?} flushed in {}ms, and {} frees",
            next_index.log.len(),
            next_index.log.num_bytes() / 1024 / 1024,
            next_index.last_key, timer.elapsed().as_millis(),
            free_count,);
        Self::Progress(message)
    }
}

struct MergeState {
    old_pending_changes: BTreeMap<IndexKey, PendingChange>,
    old_operation_log_phys: BlockBasedLogPhys<OperationLogEntry>,
    eviction_cutoff: Atime,
}

impl MergeState {
    /// Add an entry to the new index, or evict it if it's atime is before the eviction cut off.
    fn add_to_index_or_evict(
        &self,
        entry: IndexEntry,
        index: &mut ZettaCacheIndex,
        free_list: &mut Vec<IndexValue>,
    ) {
        trace!("add-or-evict {:?}", entry);
        if entry.value.atime >= self.eviction_cutoff {
            index.append(entry);
        } else {
            free_list.push(entry.value);
            index.update_last_key(entry.key);
        }
    }

    /// This function runs in an async task to merge a set of pending changes with the current on-disk
    /// index in order to produce a new up-to-date on-disk index. It sends periodic "progress updates"
    /// (including block frees) to the checkpoint task.
    async fn merge_task(
        &self,
        tx: tokio::sync::mpsc::Sender<MergeMessage>,
        old_index: Arc<tokio::sync::RwLock<ZettaCacheIndex>>,
        mut next_index: ZettaCacheIndex,
    ) {
        let begin = Instant::now();
        let old_index = old_index.read().await;
        info!(
            "writing new index to merge {} pending changes into index of {} entries ({} MB)",
            self.old_pending_changes.len(),
            old_index.log.len(),
            old_index.log.num_bytes() / 1024 / 1024,
        );

        let mut free_list: Vec<IndexValue> = Vec::new();
        let mut timer = Instant::now();

        let start_key = next_index.last_key;
        info!("using {:?} as start key for merge", start_key);
        let mut pending_changes_iter = self
            .old_pending_changes
            .range((start_key.map_or(Unbounded, Excluded), Unbounded))
            .peekable();

        let mut index_stream = Box::pin(old_index.log.iter());
        let mut index_skips = 0;
        let mut count = 0;
        while let Some(entry) = index_stream.next().await {
            count += 1;
            // This can be a tight loop, so only check the elapsed time every
            // 100 times through, so that .elapsed() doesn't take significant
            // CPU time.
            if count >= *MERGE_PROGRESS_CHECK_COUNT
                && timer.elapsed() >= *MERGE_PROGRESS_MESSAGE_INTERVAL
            {
                // send free_list and current index phys to checkpointer
                tx.send(MergeMessage::new_progress(&mut next_index, free_list).await)
                    .await
                    .unwrap_or_else(|e| panic!("couldn't send: {}", e));
                free_list = Vec::new();
                timer = Instant::now();
                count = 0;
            }
            // If the next index is already "started", advance the old index to the start point
            // XXX - would be nice to simply *start* from the start_key, rather than iterate up to it
            if let Some(start_key) = start_key {
                if entry.key <= start_key {
                    trace!("skipping index entry: {:?}", entry.key);
                    index_skips += 1;
                    continue;
                }
            }
            // First, process any pending changes which are before this
            // index entry, which must be all Inserts (Removes,
            // RemoveThenInserts, and AtimeUpdates refer to existing Index
            // entries).
            trace!("next index entry: {:?}", entry);
            while let Some((&pc_key, &PendingChange::Insert(pc_value))) =
                pending_changes_iter.peek()
            {
                if pc_key >= entry.key {
                    break;
                }
                // Add this new entry to the index
                self.add_to_index_or_evict(
                    IndexEntry {
                        key: pc_key,
                        value: pc_value,
                    },
                    &mut next_index,
                    &mut free_list,
                );
                pending_changes_iter.next();
            }

            let next_pc_opt = pending_changes_iter.peek();
            match next_pc_opt {
                Some((&pc_key, &PendingChange::Remove())) => {
                    if pc_key == entry.key {
                        // Don't write this entry to the new generation.
                        // this pending change is consumed
                        pending_changes_iter.next();
                    } else {
                        // There shouldn't be a pending removal of an entry that doesn't exist in the index.
                        assert_gt!(pc_key, entry.key);
                        self.add_to_index_or_evict(entry, &mut next_index, &mut free_list);
                    }
                }
                Some((&pc_key, &PendingChange::Insert(_pc_value))) => {
                    // Insertions are processed above.  There can't be an
                    // index entry with the same key.  If there were, it has
                    // to be removed first, resulting in a
                    // PendingChange::RemoveThenInsert.
                    assert_gt!(pc_key, entry.key);
                    self.add_to_index_or_evict(entry, &mut next_index, &mut free_list);
                }
                Some((&pc_key, &PendingChange::RemoveThenInsert(pc_value))) => {
                    if pc_key == entry.key {
                        // This key must have been removed (evicted) and then re-inserted.
                        // Add the pending change to the next generation instead of the current index's entry
                        assert_eq!(pc_value.size, entry.value.size);
                        self.add_to_index_or_evict(
                            IndexEntry {
                                key: pc_key,
                                value: pc_value,
                            },
                            &mut next_index,
                            &mut free_list,
                        );

                        // this pending change is consumed
                        pending_changes_iter.next();
                    } else {
                        // We shouldn't have skipped any, because there has to be a corresponding Index entry
                        assert_gt!(pc_key, entry.key);
                        self.add_to_index_or_evict(entry, &mut next_index, &mut free_list);
                    }
                }
                Some((&pc_key, &PendingChange::UpdateAtime(pc_value))) => {
                    if pc_key == entry.key {
                        // Add the pending entry to the next generation instead of the current index's entry
                        assert_eq!(pc_value.location, entry.value.location);
                        assert_eq!(pc_value.size, entry.value.size);
                        self.add_to_index_or_evict(
                            IndexEntry {
                                key: pc_key,
                                value: pc_value,
                            },
                            &mut next_index,
                            &mut free_list,
                        );

                        // this pending change is consumed
                        pending_changes_iter.next();
                    } else {
                        // We shouldn't have skipped any, because there has to be a corresponding Index entry
                        assert_gt!(pc_key, entry.key);
                        self.add_to_index_or_evict(entry, &mut next_index, &mut free_list);
                    }
                }
                None => {
                    // no more pending changes
                    self.add_to_index_or_evict(entry, &mut next_index, &mut free_list);
                }
            }
        }
        let mut count = 0;
        while let Some((&pc_key, &PendingChange::Insert(pc_value))) = pending_changes_iter.peek() {
            count += 1;
            if count >= *MERGE_PROGRESS_CHECK_COUNT
                && timer.elapsed() >= *MERGE_PROGRESS_MESSAGE_INTERVAL
            {
                // send free_list and current index phys to checkpointer
                tx.send(MergeMessage::new_progress(&mut next_index, free_list).await)
                    .await
                    .unwrap_or_else(|e| panic!("couldn't send: {}", e));
                free_list = Vec::new();
                timer = Instant::now();
                count = 0;
            }
            // Add this new entry to the index
            trace!(
                "remaining pending change, appending to new index: {:?} {:?}",
                pc_key,
                pc_value
            );
            self.add_to_index_or_evict(
                IndexEntry {
                    key: pc_key,
                    value: pc_value,
                },
                &mut next_index,
                &mut free_list,
            );
            // Consume pending change.  We don't do that in the `while let`
            // because we want to leave any unmatched items in the iterator so
            // that we can print them out when failing below.
            pending_changes_iter.next();
        }
        // Other pending changes refer to existing index entries and therefore should have been processed above
        assert!(
            pending_changes_iter.peek().is_none(),
            "next={:?}",
            pending_changes_iter.peek().unwrap()
        );
        debug!("skipped {} index entries", index_skips);

        drop(old_index);
        next_index.flush().await;

        trace!("new histogram: {:#?}", next_index.atime_histogram);
        info!(
            "wrote next index with {} entries ({} MB) in {:.1}s ({:.1}MB/s)",
            next_index.log.len(),
            next_index.log.num_bytes() / 1024 / 1024,
            begin.elapsed().as_secs_f64(),
            (next_index.log.num_bytes() as f64 / 1024f64 / 1024f64) / begin.elapsed().as_secs_f64(),
        );

        // send the now complete next_index as the final message
        tx.send(MergeMessage::Complete(next_index))
            .await
            .unwrap_or_else(|e| panic!("couldn't send: {}", e));
        trace!("sent final checkpoint message");
    }
}

struct ZettaCacheState {
    block_access: Arc<BlockAccess>,
    super_phys: ZettaSuperBlockPhys,
    block_allocator: BlockAllocator,
    pending_changes: BTreeMap<IndexKey, PendingChange>,
    // Keep state associated with any on-going merge here
    merging_state: Option<Arc<MergeState>>,
    // XXX Given that we have to lock the entire State to do anything, we might
    // get away with this being a Rc?  And the ExtentAllocator doesn't really
    // need the lock inside it.  But hopefully we split up the big State lock
    // and then this is useful.  Same goes for block_access.
    extent_allocator: Arc<ExtentAllocator>,
    atime_histogram: AtimeHistogramPhys, // includes pending_changes, including AtimeUpdate which is not logged
    // XXX move this to its own file/struct with methods to load, etc?
    operation_log: BlockBasedLog<OperationLogEntry>,
    // When i/o completes, the value will be sent, and the entry can be removed
    // from the tree.  These are needed to prevent the ExtentAllocator from
    // overwriting them while i/o is in flight, and to ensure that writes
    // complete before we complete the next checkpoint.
    // XXX I don't think we do lookups here so these could be Vec's?
    outstanding_reads: BTreeMap<IndexValue, Arc<Semaphore>>,
    outstanding_writes: BTreeMap<IndexValue, Arc<Semaphore>>,

    atime: Atime,
}

pub struct LockedKey(LockedItem<IndexKey>);

pub enum LookupResponse {
    Present((Vec<u8>, LockedKey, IndexValue)),
    Absent(LockedKey),
}

pub enum InsertSource {
    Heal,
    Read,
    SpeculativeRead,
    Write,
}

#[metered(registry=ZettaCacheMetrics)]
impl ZettaCache {
    pub async fn create(path: &str) {
        let block_access = BlockAccess::new(path).await;
        let metadata_start = *SUPERBLOCK_SIZE + u64::from(*DEFAULT_CHECKPOINT_RING_BUFFER_SIZE);
        let data_start = block_access.round_up_to_sector(
            metadata_start
                + (*DEFAULT_METADATA_SIZE_PCT / 100.0 * block_access.size() as f64)
                    .approx_as::<u64>()
                    .unwrap(),
        );
        let checkpoint = ZettaCheckpointPhys {
            generation: CheckpointId(0),
            extent_allocator: ExtentAllocatorPhys {
                first_valid_offset: metadata_start,
                last_valid_offset: data_start,
            },
            index: Default::default(),
            operation_log: Default::default(),
            last_atime: Atime(0),
            block_allocator: BlockAllocatorPhys::new(data_start, block_access.size() - data_start),
            merge_progress: None,
        };
        let raw = block_access.chunk_to_raw(EncodeType::Json, &checkpoint);
        assert_le!(raw.len(), *DEFAULT_CHECKPOINT_RING_BUFFER_SIZE as usize);
        let checkpoint_size = raw.len() as u64;
        block_access
            .write_raw(
                DiskLocation {
                    offset: *SUPERBLOCK_SIZE,
                },
                raw,
            )
            .await;
        let phys = ZettaSuperBlockPhys {
            checkpoint_ring_buffer_size: *DEFAULT_CHECKPOINT_RING_BUFFER_SIZE,
            slab_size: *DEFAULT_SLAB_SIZE,
            last_checkpoint_extent: Extent {
                location: DiskLocation {
                    offset: *SUPERBLOCK_SIZE,
                },
                size: checkpoint_size,
            },
            last_checkpoint_id: CheckpointId(0),
        };
        phys.write(&block_access).await;
    }

    pub async fn zcachedb_dump_structures(path: &str, opts: DumpStructuresOptions) {
        let block_access = Arc::new(BlockAccess::new(path).await);

        let superblock = match ZettaSuperBlockPhys::read(&block_access).await {
            Ok(phys) => phys,
            Err(e) => {
                println!("Couldn't read ZettaCache SuperBlock!");
                println!("{:?}", e);
                return;
            }
        };
        if opts.dump_defaults {
            println!("{:#?}", superblock);
        }

        let checkpoint =
            ZettaCheckpointPhys::read(&block_access, superblock.last_checkpoint_extent).await;
        if opts.dump_defaults {
            println!("{:#?}", checkpoint);
        }

        let extent_allocator = Arc::new(ExtentAllocator::open(&checkpoint.extent_allocator));

        if opts.dump_spacemaps {
            zcachedb_dump_spacemaps(block_access, extent_allocator, checkpoint.block_allocator)
                .await;
        }
    }

    pub async fn open(path: &str) -> ZettaCache {
        let block_access = Arc::new(BlockAccess::new(path).await);

        // if superblock not present, create new cache
        // XXX need a real mechanism for creating/managing the cache devices
        let phys = match ZettaSuperBlockPhys::read(&block_access).await {
            Ok(phys) => phys,
            Err(_) => {
                Self::create(path).await;
                ZettaSuperBlockPhys::read(&block_access).await.unwrap()
            }
        };

        let checkpoint =
            ZettaCheckpointPhys::read(&block_access, phys.last_checkpoint_extent).await;

        assert_eq!(checkpoint.generation, phys.last_checkpoint_id);

        let metadata_start = *SUPERBLOCK_SIZE + u64::from(phys.checkpoint_ring_buffer_size);
        // XXX pass in the metadata_start to ExtentAllocator::open, rather than
        // having this represented twice in the on-disk format?
        assert_eq!(
            metadata_start,
            checkpoint.extent_allocator.first_valid_offset
        );
        let extent_allocator = Arc::new(ExtentAllocator::open(&checkpoint.extent_allocator));

        let operation_log = BlockBasedLog::open(
            block_access.clone(),
            extent_allocator.clone(),
            checkpoint.operation_log,
        );

        let index = ZettaCacheIndex::open(
            block_access.clone(),
            extent_allocator.clone(),
            checkpoint.index,
        )
        .await;

        // XXX would be nice to periodically load the operation_log and verify
        // that our state's pending_changes & atime_histogram match it
        let mut atime_histogram = index.atime_histogram.clone();
        let pending_changes = Self::load_operation_log(&operation_log, &mut atime_histogram).await;
        debug!("atime_histogram: {:#?}", atime_histogram);

        let state = ZettaCacheState {
            block_access: block_access.clone(),
            pending_changes,
            merging_state: None,
            atime_histogram,
            operation_log,
            super_phys: phys,
            outstanding_reads: Default::default(),
            outstanding_writes: Default::default(),
            atime: checkpoint.last_atime,
            block_allocator: BlockAllocator::open(
                block_access.clone(),
                extent_allocator.clone(),
                checkpoint.block_allocator,
            )
            .await,
            extent_allocator,
        };

        let this = ZettaCache {
            block_access,
            index: Arc::new(tokio::sync::RwLock::new(index)),
            state: Arc::new(tokio::sync::Mutex::new(state)),
            outstanding_lookups: LockSet::new(),
            metrics: Default::default(),
            blocking_buffer_bytes_available: Arc::new(Semaphore::new(
                *CACHE_INSERT_BLOCKING_BUFFER_BYTES,
            )),
            nonblocking_buffer_bytes_available: Arc::new(Semaphore::new(
                *CACHE_INSERT_NONBLOCKING_BUFFER_BYTES,
            )),
        };

        let (merge_rx, merge_index) = match checkpoint.merge_progress {
            Some((old_operation_log_phys, next_index)) => (
                Some(
                    this.state
                        .lock()
                        .await
                        .resume_merge_task(
                            this.index.clone(),
                            next_index.clone(),
                            old_operation_log_phys,
                        )
                        .await,
                ),
                Some(next_index),
            ),
            None => (None, None),
        };

        let my_cache = this.clone();
        tokio::spawn(async move {
            my_cache.checkpoint_task(merge_rx, merge_index).await;
        });

        let my_cache = this.clone();
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(10));
            loop {
                interval.tick().await;
                debug!("metrics: {:#?}", my_cache.metrics);
                my_cache.state.lock().await.block_access.dump_metrics();
            }
        });

        let my_cache = this.clone();
        tokio::spawn(async move {
            // XXX maybe we should bump the atime after a set number of
            // accesses, so each histogram bucket starts with the same count.
            // We could then add an auxiliary structure saying what wall clock
            // time each atime value corresponds to.
            let mut interval = tokio::time::interval(Duration::from_secs(10));
            loop {
                interval.tick().await;
                let mut state = my_cache.state.lock().await;
                state.atime = state.atime.next();
            }
        });

        this
    }

    /// Load the provided operation log to produce a new pending changes map.
    /// Update the atime_histogram with the data from the pending changes.
    async fn load_operation_log(
        operation_log: &BlockBasedLog<OperationLogEntry>,
        atime_histogram: &mut AtimeHistogramPhys,
    ) -> BTreeMap<IndexKey, PendingChange> {
        let begin = Instant::now();
        let mut num_insert_entries: u64 = 0;
        let mut num_remove_entries: u64 = 0;
        let mut pending_changes = BTreeMap::new();
        operation_log
            .iter()
            .for_each(|entry| {
                match entry {
                    OperationLogEntry::Insert(key, value) => {
                        match pending_changes.entry(key) {
                            btree_map::Entry::Occupied(mut oe) => match oe.get() {
                                PendingChange::Remove() => {
                                    trace!("insert with existing removal; changing to RemoveThenInsert: {:?} {:?}", key, value);
                                    oe.insert(PendingChange::RemoveThenInsert(value));
                                }
                                pc  => {
                                    panic!(
                                        "Inserting {:?} {:?} into already existing entry {:?}",
                                        key,
                                        value,
                                        pc,
                                    );
                                }
                            },
                            btree_map::Entry::Vacant(ve) => {
                                trace!("insert {:?} {:?}", key, value);
                                ve.insert(PendingChange::Insert(value));
                            }
                        }
                        num_insert_entries += 1;
                        atime_histogram.insert(value);
                    }
                    OperationLogEntry::Remove(key, value) => {
                        match pending_changes.entry(key) {
                            btree_map::Entry::Occupied(mut oe) => match oe.get() {
                                PendingChange::Insert(value) => {
                                    trace!("remove with existing insert; clearing {:?} {:?}", key, value);
                                    oe.remove();
                                }
                                PendingChange::RemoveThenInsert(value) => {
                                    trace!("remove with existing removetheninsert; changing to remove: {:?} {:?}", key, value);
                                    oe.insert(PendingChange::Remove());
                                }
                                pc  => {
                                    panic!(
                                        "Removing {:?} from already existing entry {:?}",
                                        key,
                                        pc,
                                    );
                                }
                            },
                            btree_map::Entry::Vacant(ve) => {
                                trace!("remove {:?}", key);
                                ve.insert(PendingChange::Remove());
                            }
                        }
                        num_remove_entries += 1;
                        atime_histogram.remove(value);
                    }
                };
                future::ready(())
            })
            .await;
        info!(
            "loaded operation_log from {} inserts and {} removes into {} pending_changes in {}ms",
            num_insert_entries,
            num_remove_entries,
            pending_changes.len(),
            begin.elapsed().as_millis()
        );
        pending_changes
    }

    /// The checkpoint task is primarily responsible for writing out a persistent checkpoint every 60s.
    /// It is also responsible for kicking off a merge task every time we accumulate enough pending change.
    /// While a merge task is running, this task listens for and processes eviction requests from the merge task.
    /// The active merge task state is also captured in each checkpoint so that it may be resumed from the
    /// checkpoint if necessary. On resume the merge task is restarted during cache open and a channel to
    /// task and the index phys for the current progress are passed in.
    async fn checkpoint_task(
        &self,
        mut merge_rx: Option<tokio::sync::mpsc::Receiver<MergeMessage>>,
        mut next_index: Option<ZettaCacheIndexPhys>,
    ) {
        let mut next_tick = tokio::time::Instant::now();
        loop {
            next_tick = std::cmp::max(
                tokio::time::Instant::now(),
                next_tick + *CHECKPOINT_INTERVAL,
            );
            // if there is no current merging state, check to see if a merge should be started
            if self.state.lock().await.merging_state.is_none() {
                assert!(merge_rx.is_none());
                assert!(next_index.is_none());
                merge_rx = self
                    .state
                    .lock()
                    .await
                    .try_start_merge_task(self.index.clone())
                    .await;
            }
            if let Some(rx) = &mut merge_rx {
                let mut msg_count = 0;
                let mut free_count = 0;
                // we have a channel to an active merge task, check it for messages
                loop {
                    let result = timeout_at(next_tick, rx.recv()).await;
                    match result {
                        // capture merge progress: the current next index phys and eviction requests
                        Ok(Some(MergeMessage::Progress(merge_checkpoint))) => {
                            msg_count += 1;
                            free_count += merge_checkpoint.free_list.len();
                            trace!(
                                "merge checkpoint with {} free requests",
                                merge_checkpoint.free_list.len()
                            );
                            next_index = Some(merge_checkpoint.new_index);
                            // free the extent ranges associated with the evicted blocks
                            // XXX - should check to see if the extent is still in the "coverage" area.
                            // it seems possible that the meta-data area could grow during the merge cycle.
                            for value in merge_checkpoint.free_list {
                                trace!("eviction requested for {:?}", value);
                                self.state.lock().await.block_allocator.free(value.extent());
                            }
                        }
                        // merge task complete, replace the current index with the new index
                        Ok(Some(MergeMessage::Complete(new_index))) => {
                            let mut index = self.index.write().await;
                            self.state
                                .lock()
                                .await
                                .rotate_index(&mut index, new_index)
                                .await;
                            next_index = None;
                            merge_rx = None;
                            break;
                        }
                        Ok(None) => panic!("channel closed before Complete message received"),
                        Err(_) => break, // timed out
                    }
                }
                debug!(
                    "processed {} merge checkpoints with {} evictions requested",
                    msg_count, free_count,
                );
            }

            // flush out a new checkpoint every CHECKPOINT_INTERVAL to capture the current state
            sleep_until(next_tick).await;
            {
                let index = self.index.read().await;
                self.state
                    .lock()
                    .await
                    .flush_checkpoint(&index, next_index.clone())
                    .await;
            }
        }
    }

    #[measure(HitCount)]
    fn cache_miss_without_index_read(&self, key: &IndexKey) {
        trace!("cache miss without reading index for {:?}", key);
    }

    #[measure(HitCount)]
    fn cache_miss_after_index_read(&self, key: &IndexKey) {
        trace!("cache miss after reading index for {:?}", key);
    }

    #[measure(HitCount)]
    fn cache_hit_without_index_read(&self, key: &IndexKey) {
        trace!("cache hit without reading index for {:?}", key);
    }

    #[measure(HitCount)]
    fn cache_hit_after_index_read(&self, key: &IndexKey) {
        trace!("cache hit after reading index for {:?}", key);
    }

    #[measure(HitCount)]
    fn insert_failed_max_queue_depth(&self, key: &IndexKey) {
        trace!("insertion failed due to max queue depth for {:?}", key);
    }

    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    pub async fn lookup(&self, guid: PoolGuid, block: BlockId) -> LookupResponse {
        // Hold the index lock over the whole operation
        // so that the index can't change after we get the value from it.
        // Lock ordering requres that we lock the index before locking the state.
        let key = IndexKey { guid, block };
        let locked_key = LockedKey(self.outstanding_lookups.lock(key).await);
        let index = self.index.read().await;
        let read_data_fut_opt = {
            // We don't want to hold the state lock while reading from disk so we
            // use lock_non_send() to ensure that we can't hold it across .await.
            let mut state = self.state.lock_non_send().await;

            match state.pending_changes.get(&key).copied() {
                Some(pc) => {
                    match pc {
                        PendingChange::Insert(value)
                        | PendingChange::RemoveThenInsert(value)
                        | PendingChange::UpdateAtime(value) => Some(state.lookup(key, value)),
                        PendingChange::Remove() => {
                            // Pending change says this has been removed
                            Some(data_reader_none())
                        }
                    }
                }
                None => {
                    // No pending change in current state; need to look in merging state
                    if let Some(ms) = &state.merging_state {
                        let eviction_cutoff = ms.eviction_cutoff;
                        ms.old_pending_changes
                            .get(&key)
                            .copied()
                            .map(|pc| match pc {
                                PendingChange::Insert(value)
                                | PendingChange::RemoveThenInsert(value)
                                | PendingChange::UpdateAtime(value) => {
                                    // if this block's atime is before the eviction cutoff, return none
                                    if value.atime >= eviction_cutoff {
                                        state.lookup(key, value)
                                    } else {
                                        data_reader_none()
                                    }
                                }
                                PendingChange::Remove() => {
                                    // Pending change says this has been removed
                                    data_reader_none()
                                }
                            })
                    } else {
                        None
                    }
                }
            }
        };

        if let Some(read_data_fut) = read_data_fut_opt {
            // pending state tells us what to do
            match read_data_fut.await {
                Some((vec, value)) => {
                    self.cache_hit_without_index_read(&key);
                    return LookupResponse::Present((vec, locked_key, value));
                }
                None => {
                    self.cache_miss_without_index_read(&key);
                    return LookupResponse::Absent(locked_key);
                }
            }
        }

        trace!("lookup has no pending_change; checking index for {:?}", key);
        match index.log.lookup_by_key(&key, |entry| entry.key).await {
            None => {
                // key not in index
                self.cache_miss_after_index_read(&key);
                LookupResponse::Absent(locked_key)
            }
            Some(entry) => {
                // Again, we don't want to hold the state lock while reading from disk so
                // we use lock_non_send() to ensure that we can't hold it across .await.
                let read_data_fut = {
                    let mut state = self.state.lock_non_send().await;

                    if let Some(ms) = &state.merging_state {
                        if entry.value.atime < ms.eviction_cutoff {
                            // Block is being evicted, abort the read attempt
                            self.cache_miss_after_index_read(&key);
                            return LookupResponse::Absent(locked_key);
                        }
                    }

                    state.lookup_with_value_from_index(key, Some(entry.value))
                };

                // read data from location indicated by index
                match read_data_fut.await {
                    Some((vec, value)) => {
                        self.cache_hit_after_index_read(&key);
                        // We return the IndexValue from the DataReader, which
                        // may be different from entry.value if we found it in a
                        // PendingChange.
                        LookupResponse::Present((vec, locked_key, value))
                    }
                    None => {
                        self.cache_miss_after_index_read(&key);
                        LookupResponse::Absent(locked_key)
                    }
                }
            }
        }
    }

    /// Initiates insertion of this block; doesn't wait for the write to disk.
    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    pub async fn insert(&self, locked_key: LockedKey, buf: Vec<u8>, source: InsertSource) {
        // The passed in buffer is only for a single block, which is capped to SPA_MAXBLOCKSIZE,
        // and thus we should never have an issue converting the length to a "u32" here.
        let bytes = u32::try_from(buf.len()).unwrap();

        // This permit will be dropped when the write to disk completes.  It
        // serves to limit the number of insert()'s that we can buffer before
        // dropping (ignoring) insertion requests.
        let insert_permit = match source {
            InsertSource::Heal | InsertSource::SpeculativeRead | InsertSource::Write => match self
                .nonblocking_buffer_bytes_available
                .clone()
                .try_acquire_many_owned(bytes)
            {
                Ok(permit) => permit,
                Err(tokio::sync::TryAcquireError::NoPermits) => {
                    self.insert_failed_max_queue_depth(locked_key.0.value());
                    return;
                }
                Err(e) => panic!("unexpected error from try_acquire_many_owned: {:?}", e),
            },
            InsertSource::Read => match self
                .blocking_buffer_bytes_available
                .clone()
                .acquire_many_owned(bytes)
                .await
            {
                Ok(permit) => permit,
                Err(e) => panic!("unexpected error from acquire_many_owned: {:?}", e),
            },
        };

        let block_access = self.block_access.clone();
        let state = self.state.clone();
        tokio::spawn(async move {
            // Get a permit to write to disk before waiting on the state lock.
            // This ensures that once we assign this insertion to a checkpoint,
            // the insertion will complete relatively quickly (e.g.
            // milliseconds).  This way, we don't have outstanding_writes that
            // take a long time to complete, preventing a checkpoint from making
            // progress.  Acquiring the WritePermit may take a long time,
            // because we have to wait for any in-progress insertions (up to
            // CACHE_INSERT_MAX_BUFFER) to complete before we can write to disk.
            let write_permit = block_access.acquire_write().await;
            // Now that we are ready to issue the write to disk, insert to the
            // cache in the current checkpoint (allocate a block, add to
            // pending_changes and outstanding_writes).
            let fut =
                state
                    .lock_non_send()
                    .await
                    .insert(insert_permit, write_permit, locked_key, buf);
            fut.await;
        });
    }

    pub async fn evict(&self, key: IndexKey, value: IndexValue) {
        let mut state = self.state.lock_non_send().await;
        state.remove_from_index(key, value);
        state.block_allocator.free(value.extent());
    }

    #[measure(HitCount)]
    fn healed_by_overwriting(&self, key: &IndexKey, value: &IndexValue) {
        debug!("Healing by overwriting: {:?} {:?}", key, value);
    }

    #[measure(HitCount)]
    fn healed_by_evicting(&self, key: &IndexKey, value: &IndexValue, new_size: usize) {
        debug!(
            "Healing wrong-size entry (new size {}) by evicting & reinserting: {:?} {:?}",
            new_size, key, value
        );
    }

    pub async fn heal(&self, guid: PoolGuid, block: BlockId, buf: &[u8]) {
        if let LookupResponse::Present((cached_buf, locked_key, value)) =
            self.lookup(guid, block).await
        {
            if cached_buf != buf {
                if buf.len() == value.size as usize {
                    // overwrite with correct data
                    self.healed_by_overwriting(locked_key.0.value(), &value);
                    self.block_access
                        .write_raw(value.location, buf.to_owned())
                        .await;
                } else {
                    // size differs; evict from cache and reinsert
                    self.healed_by_evicting(locked_key.0.value(), &value, buf.len());
                    {
                        let mut state = self.state.lock_non_send().await;
                        state.remove_from_index(*locked_key.0.value(), value);
                        state.block_allocator.free(value.extent());
                    }
                    self.insert(locked_key, buf.to_owned(), InsertSource::Heal)
                        .await;
                }
            }
        }
    }
}

type DataReader = Pin<Box<dyn Future<Output = Option<(Vec<u8>, IndexValue)>> + Send>>;

fn data_reader_none() -> DataReader {
    Box::pin(async move { None })
}

impl ZettaCacheState {
    fn lookup_with_value_from_index(
        &mut self,
        key: IndexKey,
        value_from_index_opt: Option<IndexValue>,
    ) -> DataReader {
        // Note: we're here because there was no PendingChange for this key, but
        // since we dropped the lock, a PendingChange may have been inserted
        // since then.  So we need to check for a PendingChange before using the
        // value from the index.
        // XXX is this still true, given that now we have the LockedKey
        // (outstanding_lookups lock)?
        let value = match self.pending_changes.get(&key) {
            Some(PendingChange::Insert(value_ref))
            | Some(PendingChange::RemoveThenInsert(value_ref))
            | Some(PendingChange::UpdateAtime(value_ref)) => *value_ref,
            Some(PendingChange::Remove()) => return data_reader_none(),
            None => {
                // use value from on-disk index
                match value_from_index_opt {
                    Some(value) => value,
                    None => return data_reader_none(),
                }
            }
        };

        self.lookup(key, value)
    }

    fn lookup(&mut self, key: IndexKey, mut value: IndexValue) -> DataReader {
        if value.location.offset < self.extent_allocator.get_phys().last_valid_offset {
            // The metadata overwrote this data, so it's no longer in the cache.
            // Remove from index and return None.
            trace!(
                "cache miss: {:?} at {:?} was overwritten by metadata allocator; removing from cache",
                key,
                value
            );
            // Note: we could pass in the (mutable) pending_change reference,
            // which would let evict_block() avoid looking it up again.  But
            // this is not a common code path, and this interface seems cleaner.
            self.remove_from_index(key, value);
            return data_reader_none();
        }
        // If value.atime is before eviction cutoff, return a cache miss
        if let Some(ms) = &self.merging_state {
            if value.atime < ms.eviction_cutoff {
                trace!(
                    "cache miss: {:?} at {:?} was prior to eviction cutoff {:?}",
                    key,
                    value,
                    ms.eviction_cutoff
                );
                return data_reader_none();
            }
        }
        trace!("cache hit: reading {:?} from {:?}", key, value);
        if value.atime != self.atime {
            self.atime_histogram.remove(value);
            value.atime = self.atime;
            self.atime_histogram.insert(value);
        }
        // XXX looking up again.  But can't pass in both &mut self and &mut PendingChange
        let pc = self.pending_changes.get_mut(&key);
        match pc {
            Some(PendingChange::Insert(value_ref))
            | Some(PendingChange::RemoveThenInsert(value_ref))
            | Some(PendingChange::UpdateAtime(value_ref)) => {
                *value_ref = value;
            }
            Some(PendingChange::Remove()) => panic!("invalid state"),
            None => {
                // only in Index, not pending_changes
                trace!(
                    "adding UpdateAtime to pending_changes {:?} {:?}",
                    key,
                    value
                );
                // XXX would be nice to have saved the btreemap::Entry so we
                // don't have to traverse the tree again.
                self.pending_changes
                    .insert(key, PendingChange::UpdateAtime(value));
            }
        }

        // If there's a write to this location in progress, we will need to wait for it to complete before reading.
        // Since we won't be able to remove the entry from outstanding_writes after we wait, we just get the semaphore.
        let write_sem_opt = self
            .outstanding_writes
            .get_mut(&value)
            .map(|arc| arc.clone());

        let sem = Arc::new(Semaphore::new(0));
        self.outstanding_reads.insert(value, sem.clone());
        let block_access = self.block_access.clone();
        Box::pin(async move {
            if let Some(write_sem) = write_sem_opt {
                trace!("{:?} at {:?}: waiting for outstanding write", key, value);
                let _permit = write_sem.acquire().await.unwrap();
            }

            let vec = block_access.read_raw(value.extent()).await;
            sem.add_permits(1);
            // XXX we can easily handle an io error here by returning None
            Some((vec, value))
        })
    }

    fn remove_from_index(&mut self, key: IndexKey, value: IndexValue) {
        match self.pending_changes.get_mut(&key) {
            Some(PendingChange::Insert(value_ref)) => {
                // The operation_log has an Insert for this key, and the key
                // is not in the Index.  We don't need a
                // PendingChange::Removal since there's nothing to remove
                // from the index.
                assert_eq!(*value_ref, value);
                trace!("removing Insert from pending_changes {:?} {:?}", key, value);
                self.pending_changes.remove(&key);
            }
            Some(PendingChange::RemoveThenInsert(value_ref)) => {
                // The operation_log has a Remove, and then an Insert for
                // this key, so the key is in the Index.  We need a
                // PendingChange::Remove so that the Index entry won't be
                // found.
                assert_eq!(*value_ref, value);
                trace!(
                    "changing RemoveThenInsert to Remove in pending_changes {:?} {:?}",
                    key,
                    value,
                );
                self.pending_changes.insert(key, PendingChange::Remove());
            }
            Some(PendingChange::UpdateAtime(value_ref)) => {
                // It's just an atime update, so the operation_log doesn't
                // have an Insert for this key, but the key is in the
                // Index.
                assert_eq!(*value_ref, value);
                trace!(
                    "changing UpdateAtime to Remove in pending_changes {:?} {:?}",
                    key,
                    value,
                );
                self.pending_changes.insert(key, PendingChange::Remove());
            }
            Some(PendingChange::Remove()) => {
                panic!("invalid state");
            }
            None => {
                // only in Index, not pending_changes
                trace!("adding Remove to pending_changes {:?}", key);
                self.pending_changes.insert(key, PendingChange::Remove());
            }
        }
        trace!("adding Remove to operation_log {:?}", key);
        self.atime_histogram.remove(value);
        self.operation_log
            .append(OperationLogEntry::Remove(key, value));
    }

    /// Insert this block to the cache, if space and performance parameters
    /// allow.  It may be a recent cache miss, or a recently-written block.
    /// Returns a Future to be executed after the state lock has been dropped.
    fn insert(
        &mut self,
        insert_permit: OwnedSemaphorePermit,
        write_permit: WritePermit,
        locked_key: LockedKey,
        buf: Vec<u8>,
    ) -> impl Future {
        let buf_size = buf.len();
        let aligned_size = self.block_access.round_up_to_sector(buf_size);

        let aligned_buf = if buf_size == aligned_size {
            buf
        } else {
            // pad to sector size
            let mut tail: Vec<u8> = Vec::new();
            tail.resize(aligned_size - buf_size, 0);
            // XXX copying data around; have caller pass in larger buffer?  or
            // at least let us pass the unaligned buf to write_raw() which
            // already has to copy it around to get the pointer aligned.
            [buf, tail].concat()
        };
        let location_opt = self.allocate_block(u32::try_from(aligned_buf.len()).unwrap());
        if location_opt.is_none() {
            return future::Either::Left(async {});
        }
        let location = location_opt.unwrap();

        // XXX if this is past the last block of the main index, we can write it
        // there (and location_dirty:false) instead of logging it

        let key = *locked_key.0.value();
        let value = IndexValue {
            atime: self.atime,
            location,
            size: u32::try_from(buf_size).unwrap(),
        };

        // XXX we'd like to assert that this is not already in the index
        // (otherwise we would need to use a PendingChange::RemoveThenInsert).
        // However, this is not an async fn so we can't do the read here.  We
        // could spawn a new task, but currently reading the index requires the
        // big lock.

        match self.pending_changes.entry(key) {
            btree_map::Entry::Occupied(mut oe) => match oe.get() {
                PendingChange::Remove() => {
                    trace!(
                        "adding RemoveThenInsert to pending_changes {:?} {:?}",
                        key,
                        value
                    );
                    oe.insert(PendingChange::RemoveThenInsert(value));
                }
                _ => {
                    // Already in cache; ignore this insertion request?  Or panic?
                    todo!();
                }
            },
            btree_map::Entry::Vacant(ve) => {
                trace!("adding Insert to pending_changes {:?} {:?}", key, value);
                ve.insert(PendingChange::Insert(value));
            }
        }
        self.atime_histogram.insert(value);

        trace!("adding Insert to operation_log {:?} {:?}", key, value);
        self.operation_log
            .append(OperationLogEntry::Insert(key, value));

        let sem = Arc::new(Semaphore::new(0));
        self.outstanding_writes.insert(value, sem.clone());

        let block_access = self.block_access.clone();
        // Note: locked_key can be dropped before the i/o completes, since the
        // changes to the State have already been made.  We want to hold onto
        // the insert_permit until the write completes because it represents the
        // memory that's required to buffer this insertion, which isn't released
        // until the io completes.
        future::Either::Right(async move {
            block_access
                .write_raw_permit(write_permit, location, aligned_buf)
                .await
                .unwrap();
            sem.add_permits(1);
            drop(insert_permit);
        })
    }

    /// returns offset, or None if there's no space
    fn allocate_block(&mut self, size: u32) -> Option<DiskLocation> {
        self.block_allocator
            .allocate(size)
            .map(|extent| extent.location)
    }

    /// Flush out the current set of pending index changes. This is a recovery point in case of
    /// a system crash between index rewrites.
    async fn flush_checkpoint(
        &mut self,
        index: &ZettaCacheIndex,
        next_index: Option<ZettaCacheIndexPhys>,
    ) {
        debug!(
            "flushing checkpoint {:?}",
            self.super_phys.last_checkpoint_id.next()
        );
        let begin_checkpoint = Instant::now();

        // Wait for all outstanding reads, so that if the ExtentAllocator needs
        // to overwrite some blocks, there aren't any outstanding i/os to that
        // region.
        // XXX It would be better to only wait for the reads that are in the
        // region that we're overwriting.  But it will be tricky to do the
        // waiting down in the ExtentAllocator.  If we get that working, we'll
        // still need to clean up the outstanding_reads entries that have
        // completed, at some point.  Even as-is, letting them accumulate for a
        // whole checkpoint might not be great.  It might be "cleaner" to
        // run every second and remove completed entries.  Or have the read task
        // lock the outstanding_reads and remove itself (which might perform
        // worse due to contention on the global lock).
        let begin = Instant::now();
        for sem in self.outstanding_reads.values_mut() {
            let _permit = sem.acquire().await.unwrap();
        }
        debug!(
            "waited for {} outstanding_reads in {}ms",
            self.outstanding_reads.len(),
            begin.elapsed().as_millis()
        );
        self.outstanding_reads.clear();

        // Wait for all outstanding writes, for the same reason as reads, and
        // also so that if we crash, the blocks referenced by the
        // index/operation_log will actually have the correct contents.
        let begin = Instant::now();
        for sem in self.outstanding_writes.values_mut() {
            let _permit = sem.acquire().await.unwrap();
        }
        debug!(
            "waited for {} outstanding_writes in {}ms",
            self.outstanding_writes.len(),
            begin.elapsed().as_millis()
        );
        self.outstanding_writes.clear();

        trace!(
            "{:?} pending changes at checkpoint",
            self.pending_changes.len()
        );

        let begin = Instant::now();
        let operation_log_len = self.operation_log.pending_len();
        let bytes = self.operation_log.num_bytes();
        let operation_log_phys = self.operation_log.flush().await;
        let operation_log_bytes = self.operation_log.num_bytes() - bytes;
        debug!(
            "operation log: flushed {} entries to {} KB in {}ms",
            operation_log_len,
            operation_log_bytes / 1024,
            begin.elapsed().as_millis()
        );

        // Note that it is possible to have a merge in progress with no next_index available.
        // This can happen if we have not yet received any progress messages from the merge task.
        // In this case we just store an empty "in progress" index in the checkpoint.
        let merge_progress = self.merging_state.as_ref().map(|ms| {
            (
                ms.old_operation_log_phys.clone(),
                next_index.unwrap_or_default(),
            )
        });

        let checkpoint = ZettaCheckpointPhys {
            generation: self.super_phys.last_checkpoint_id.next(),
            extent_allocator: self.extent_allocator.get_phys(),
            index: index.get_phys(),
            operation_log: operation_log_phys,
            last_atime: self.atime,
            block_allocator: self.block_allocator.flush().await,
            merge_progress,
        };

        let mut checkpoint_location = self.super_phys.last_checkpoint_extent.location
            + self.super_phys.last_checkpoint_extent.size;

        let raw = self
            .block_access
            .chunk_to_raw(EncodeType::Json, &checkpoint);
        if raw.len()
            > usize::from64(
                checkpoint.extent_allocator.first_valid_offset - checkpoint_location.offset,
            )
        {
            // Out of space; go back to the beginning of the checkpoint space.
            checkpoint_location.offset = *SUPERBLOCK_SIZE;
            assert_le!(
                raw.len(),
                usize::from64(
                    self.super_phys.last_checkpoint_extent.location.offset
                        - checkpoint_location.offset
                ),
            );
            // XXX The above assertion could fail if there isn't enough
            // checkpoint space for 3 checkpoints (the existing one that
            // we're writing before, the one we're writing, and the space
            // after the existing one that we aren't using).  Note that we
            // could in theory reduce this to 2 checkpoints if we allowed a
            // single checkpoint to wrap around (part of it at the end and
            // then part at the beginning of the space).
        }
        maybe_die_with(|| format!("before writing {:#?}", checkpoint));
        debug!("writing to {:?}: {:#?}", checkpoint_location, checkpoint);

        self.super_phys.last_checkpoint_extent = Extent {
            location: checkpoint_location,
            size: raw.len() as u64,
        };
        self.block_access.write_raw(checkpoint_location, raw).await;

        self.super_phys.last_checkpoint_id = self.super_phys.last_checkpoint_id.next();
        self.super_phys.write(&self.block_access).await;

        self.extent_allocator.checkpoint_done();

        info!(
            "completed {:?} in {}ms; flushed {} operations ({}KB) to log",
            self.super_phys.last_checkpoint_id,
            begin_checkpoint.elapsed().as_millis(),
            operation_log_len,
            operation_log_bytes / 1024,
        );
    }

    /// Restart a merge task from the saved checkpoint state
    async fn resume_merge_task(
        &mut self,
        old_index: Arc<tokio::sync::RwLock<ZettaCacheIndex>>,
        new_index_phys: ZettaCacheIndexPhys,
        old_operation_log_phys: BlockBasedLogPhys<OperationLogEntry>,
    ) -> tokio::sync::mpsc::Receiver<MergeMessage> {
        let old_operation_log = BlockBasedLog::open(
            self.block_access.clone(),
            self.extent_allocator.clone(),
            old_operation_log_phys.clone(),
        );
        let old_pending_changes =
            ZettaCache::load_operation_log(&old_operation_log, &mut self.atime_histogram).await;
        let next_index = ZettaCacheIndex::open(
            self.block_access.clone(),
            self.extent_allocator.clone(),
            new_index_phys,
        )
        .await;
        info!(
            "restarting merge at {:?} with eviction atime {:?}",
            next_index.last_key,
            next_index.first_atime(),
        );

        let merging_state = Arc::new(MergeState {
            eviction_cutoff: next_index.first_atime(),
            old_pending_changes,
            old_operation_log_phys,
        });
        self.merging_state = Some(merging_state.clone());

        // The checkpoint task will be constantly reading from the channel, so we don't really need
        // much of a buffer here. We use 100 because we might accumulate some messages while actually
        // flushing out the checkpoint.
        let (tx, rx) = tokio::sync::mpsc::channel(100);
        tokio::spawn(async move { merging_state.merge_task(tx, old_index, next_index).await });

        rx
    }

    /// Start a new merge task if there are enough pending changes
    async fn try_start_merge_task(
        &mut self,
        old_index: Arc<tokio::sync::RwLock<ZettaCacheIndex>>,
    ) -> Option<tokio::sync::mpsc::Receiver<MergeMessage>> {
        if self.pending_changes.len() < *MAX_PENDING_CHANGES
            && self.atime_histogram.sum()
                < (self.block_access.size() / 100) * *HIGH_WATER_CACHE_SIZE_PCT
        {
            trace!(
                "not starting new merge, only {} pending changes",
                self.pending_changes.len()
            );
            return None;
        }

        let target_size = (self.block_access.size() / 100) * *TARGET_CACHE_SIZE_PCT;
        info!(
            "target cache size for storage size {}GB is {}GB; {}MB used; {}MB high-water; {}MB freeing; histogram covers {}MB",
            self.block_access.size() / 1024 / 1024 / 1024,
            target_size / 1024 / 1024 / 1024,
            (self.block_access.size() - self.block_allocator.get_available()) / 1024 / 1024,
            (self.block_access.size() / 100) * *HIGH_WATER_CACHE_SIZE_PCT / 1024 / 1024,
            self.block_allocator.get_freeing() / 1024 / 1024,
            self.atime_histogram.sum() / 1024 / 1024,
        );
        let eviction_atime = self.atime_histogram.atime_for_target_size(target_size);

        let old_operation_log_phys = self.operation_log.flush().await;

        // Create an empty operation log that is consistent with the empty pending state.
        // Note that we don't want to just clear the existing operation log, since we are
        // still preserving that in the merging state.
        self.operation_log = BlockBasedLog::open(
            self.block_access.clone(),
            self.extent_allocator.clone(),
            Default::default(),
        );
        let next_index = ZettaCacheIndex::open(
            self.block_access.clone(),
            self.extent_allocator.clone(),
            ZettaCacheIndexPhys::new(eviction_atime),
        )
        .await;

        // Set up state with current pending changes and operation log in the new merging state.
        // Note that we are "taking" the current set of pending changes for the merge
        // and leaving an empty log behind to accumulate new changes.
        let merging_state = Arc::new(MergeState {
            eviction_cutoff: eviction_atime,
            old_pending_changes: std::mem::take(&mut self.pending_changes),
            old_operation_log_phys,
        });
        self.merging_state = Some(merging_state.clone());

        // The checkpoint task will be constantly reading from the channel, so we don't really need
        // much of a buffer here. We use 100 because we might accumulate some messages while actually
        // flushing out the checkpoint.
        let (tx, rx) = tokio::sync::mpsc::channel(100);
        tokio::spawn(async move { merging_state.merge_task(tx, old_index, next_index).await });

        Some(rx)
    }

    /// Switch to the new index returned from the merge task and clear the merging state.
    /// Called with the old index write-locked.
    async fn rotate_index(&mut self, index: &mut ZettaCacheIndex, next_index: ZettaCacheIndex) {
        let merging_state = self.merging_state.as_ref().unwrap();

        // Free up the extents that have been allocated for the merge pending state
        merging_state
            .old_operation_log_phys
            .clone()
            .clear(self.extent_allocator.clone());

        // Move the "start" of the zettacache state histogram to reflect the new index
        self.atime_histogram.reset_first(next_index.first_atime());
        trace!(
            "reset incore histogram start to {:?}",
            self.atime_histogram.first()
        );

        // Free up the space used by the old index and rotate in the new index
        index.clear();
        *index = next_index;

        self.merging_state = None;
    }
}

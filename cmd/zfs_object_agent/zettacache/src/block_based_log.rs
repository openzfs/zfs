use crate::base_types::*;
use crate::block_access::BlockAccess;
use crate::block_access::EncodeType;
use crate::extent_allocator::ExtentAllocator;
use anyhow::Context;
use async_stream::stream;
use futures::stream::FuturesUnordered;
use futures::StreamExt;
use futures_core::Stream;
use lazy_static::lazy_static;
use log::*;
use more_asserts::*;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use std::cmp::max;
use std::cmp::min;
use std::collections::BTreeMap;
use std::fmt::Debug;
use std::marker::PhantomData;
use std::ops::Add;
use std::ops::Bound::*;
use std::ops::Sub;
use std::sync::Arc;
use std::time::Instant;
use util::get_tunable;

lazy_static! {
    // XXX maybe this is wasteful for the smaller logs?
    static ref DEFAULT_EXTENT_SIZE: u64 = get_tunable("default_extent_size", 128 * 1024 * 1024);
    static ref ENTRIES_PER_CHUNK: usize = get_tunable("entries_per_chunk", 200);
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct BlockBasedLogPhys<T: BlockBasedLogEntry> {
    // XXX on-disk format could just be array of extents; offset can be derived
    // from size of previous extents. We do need the btree in RAM though so that
    // we can do random reads on the Index (unless the ChunkSummary points
    // directly to the on-disk location)
    extents: BTreeMap<LogOffset, Extent>, // offset -> disk_location, size
    next_chunk: ChunkId,
    next_chunk_offset: LogOffset, // logical byte offset of next chunk to write
    num_entries: u64,
    entry_type: PhantomData<T>,
}

// Unfortunately, #[derive(Default)] doesn't generate exactly this code; it
// requires that T: Default, which is not the case, and is not necessary here.
// See https://github.com/rust-lang/rust/issues/26925
impl<T: BlockBasedLogEntry> Default for BlockBasedLogPhys<T> {
    fn default() -> Self {
        Self {
            extents: Default::default(),
            next_chunk: Default::default(),
            next_chunk_offset: Default::default(),
            num_entries: Default::default(),
            entry_type: PhantomData,
        }
    }
}

impl<T: BlockBasedLogEntry> BlockBasedLogPhys<T> {
    pub fn clear(&mut self, extent_allocator: Arc<ExtentAllocator>) {
        for extent in self.extents.values() {
            extent_allocator.free(extent);
        }
        *self = Default::default();
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct BlockBasedLogWithSummaryPhys<T: BlockBasedLogEntry> {
    #[serde(bound(deserialize = "T: DeserializeOwned"))]
    this: BlockBasedLogPhys<T>,
    #[serde(bound(deserialize = "T: DeserializeOwned"))]
    chunk_summary: BlockBasedLogPhys<BlockBasedLogChunkSummaryEntry<T>>,
    #[serde(bound(deserialize = "T: DeserializeOwned"))]
    last_entry: Option<T>,
}

impl<T: BlockBasedLogEntry> Default for BlockBasedLogWithSummaryPhys<T> {
    fn default() -> Self {
        Self {
            this: Default::default(),
            chunk_summary: Default::default(),
            last_entry: Default::default(),
        }
    }
}

pub trait BlockBasedLogEntry: 'static + OnDisk + Copy + Clone + Unpin + Send + Sync {}

#[derive(Debug, Serialize, Deserialize, Copy, Clone)]
pub struct BlockBasedLogChunkSummaryEntry<T: BlockBasedLogEntry> {
    offset: LogOffset,
    #[serde(bound(deserialize = "T: DeserializeOwned"))]
    first_entry: T,
}
impl<T: BlockBasedLogEntry> OnDisk for BlockBasedLogChunkSummaryEntry<T> {}
impl<T: BlockBasedLogEntry> BlockBasedLogEntry for BlockBasedLogChunkSummaryEntry<T> {}

pub struct BlockBasedLog<T: BlockBasedLogEntry> {
    block_access: Arc<BlockAccess>,
    extent_allocator: Arc<ExtentAllocator>,
    phys: BlockBasedLogPhys<T>,
    pending_entries: Vec<T>,
}

pub struct BlockBasedLogWithSummary<T: BlockBasedLogEntry> {
    this: BlockBasedLog<T>,
    chunk_summary: BlockBasedLog<BlockBasedLogChunkSummaryEntry<T>>,
    chunks: Vec<BlockBasedLogChunkSummaryEntry<T>>,
    last_entry: Option<T>,
}

#[derive(Serialize, Deserialize, Debug)]
struct BlockBasedLogChunk<T: BlockBasedLogEntry> {
    id: ChunkId,
    offset: LogOffset,
    #[serde(bound(deserialize = "Vec<T>: DeserializeOwned"))]
    entries: Vec<T>,
}

impl<T: BlockBasedLogEntry> BlockBasedLog<T> {
    pub fn open(
        block_access: Arc<BlockAccess>,
        extent_allocator: Arc<ExtentAllocator>,
        phys: BlockBasedLogPhys<T>,
    ) -> BlockBasedLog<T> {
        for (_offset, extent) in phys.extents.iter() {
            extent_allocator.claim(extent);
        }
        BlockBasedLog {
            block_access,
            extent_allocator,
            phys,
            pending_entries: Vec::new(),
        }
    }

    pub async fn flush(&mut self) -> BlockBasedLogPhys<T> {
        self.flush_impl(|_, _, _| {}).await;
        self.phys.clone()
    }

    pub fn len(&self) -> u64 {
        self.phys.num_entries + self.pending_entries.len() as u64
    }

    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.phys.num_entries == 0 && self.pending_entries.is_empty()
    }

    /// Size of the on-disk representation
    pub fn num_bytes(&self) -> u64 {
        self.phys.next_chunk_offset.0
    }

    pub fn pending_len(&self) -> u64 {
        self.pending_entries.len() as u64
    }

    pub fn append(&mut self, entry: T) {
        self.pending_entries.push(entry);
        // XXX if too many pending, initiate flush?
    }

    async fn flush_impl<F>(&mut self, mut new_chunk_fn: F)
    where
        F: FnMut(ChunkId, LogOffset, T),
    {
        if self.pending_entries.is_empty() {
            return;
        }

        let writes_stream = FuturesUnordered::new();
        for pending_entries_chunk in self.pending_entries.chunks(*ENTRIES_PER_CHUNK) {
            let chunk = BlockBasedLogChunk {
                id: self.phys.next_chunk,
                offset: self.phys.next_chunk_offset,
                entries: pending_entries_chunk.to_owned(),
            };

            let first_entry = *chunk.entries.first().unwrap();

            let mut extent = self.next_write_location();
            // XXX I think we only want to use Bincode for the main index?
            let raw_chunk = self.block_access.chunk_to_raw(EncodeType::Bincode, &chunk);
            let raw_size = raw_chunk.len() as u64;
            if raw_size > extent.size {
                // free the unused tail of this extent
                self.extent_allocator.free(&extent);
                let capacity = match self.phys.extents.iter_mut().next_back() {
                    Some((last_offset, last_extent)) => {
                        last_extent.size -= extent.size;
                        LogOffset(last_offset.0 + last_extent.size)
                    }
                    None => LogOffset(0),
                };

                extent = self
                    .extent_allocator
                    .allocate(raw_size, max(raw_size, *DEFAULT_EXTENT_SIZE));
                self.phys.extents.insert(capacity, extent);
                assert_ge!(extent.size, raw_size);
            }
            // XXX add name of this log for debug purposes?
            trace!(
                "flushing BlockBasedLog: writing {:?} ({:?}) with {} entries ({} bytes) to {:?}",
                chunk.id,
                chunk.offset,
                chunk.entries.len(),
                raw_chunk.len(),
                extent.location,
            );
            // XXX would be better to aggregate lots of buffers into one write
            writes_stream.push(self.block_access.write_raw(extent.location, raw_chunk));

            new_chunk_fn(chunk.id, chunk.offset, first_entry);

            self.phys.num_entries += chunk.entries.len() as u64;
            self.phys.next_chunk = self.phys.next_chunk.next();
            self.phys.next_chunk_offset.0 += raw_size;
        }
        writes_stream.for_each(|_| async move {}).await;
        self.pending_entries.truncate(0);
    }

    pub fn clear(&mut self) {
        self.pending_entries.clear();
        self.phys.clear(self.extent_allocator.clone());
    }

    fn next_write_location(&self) -> Extent {
        match self.phys.extents.iter().next_back() {
            Some((offset, extent)) => {
                // There shouldn't be any extents after the last (partially-full) one.
                assert_ge!(self.phys.next_chunk_offset, offset);
                let offset_within_extent = self.phys.next_chunk_offset.0 - offset.0;
                // The last extent should go at least to the end of the chunks.
                assert_le!(offset_within_extent, extent.size);
                Extent {
                    location: DiskLocation {
                        offset: extent.location.offset + offset_within_extent,
                    },
                    size: extent.size - offset_within_extent,
                }
            }
            None => Extent {
                location: DiskLocation { offset: 0 },
                size: 0,
            },
        }
    }

    /// Iterates the on-disk state; panics if there are pending changes.
    fn iter_chunks(&self) -> impl Stream<Item = (BlockBasedLogChunk<T>, DiskLocation)> {
        assert!(self.pending_entries.is_empty());
        // XXX is it possible to do this without copying self.phys.extents?  Not
        // a huge deal I guess since it should be small.
        let phys = self.phys.clone();
        let block_access = self.block_access.clone();
        let next_chunk_offset = self.phys.next_chunk_offset;
        stream! {
            let mut chunk_id = ChunkId(0);
            for (offset, extent) in phys.extents.iter() {
                // XXX Probably want to do smaller i/os than the entire extent
                // (which is up to 128MB).  Also want to issue a few in
                // parallel?

                let truncated_extent =
                    extent.range(0, min(extent.size, (next_chunk_offset - *offset)));
                let extent_bytes = block_access.read_raw(truncated_extent).await;
                let mut total_consumed = 0;
                while total_consumed < extent_bytes.len() {
                    let chunk_location = DiskLocation {
                        offset: extent.location.offset + total_consumed as u64,
                    };
                    trace!("decoding {:?} from {:?}", chunk_id, chunk_location);
                    // XXX handle checksum error here
                    let (chunk, consumed): (BlockBasedLogChunk<T>, usize) = block_access
                        .chunk_from_raw(&extent_bytes[total_consumed..])
                        .with_context(|| format!("{:?} at {:?}", chunk_id, chunk_location))
                        .unwrap();
                    assert_eq!(chunk.id, chunk_id);
                    yield (chunk, chunk_location);
                    chunk_id = chunk_id.next();
                    total_consumed += consumed;
                    if chunk_id == phys.next_chunk {
                        break;
                    }
                }
            }
        }
    }

    pub fn iter(&self) -> impl Stream<Item = T> {
        let stream = self.iter_chunks();
        let phys_entries = self.phys.num_entries;

        stream! {
            let mut num_entries = 0;
            for await (chunk, _) in stream {
                for entry in chunk.entries {
                    yield entry;
                    num_entries += 1;
                }
            };
            assert_eq!(phys_entries, num_entries);
        }
    }

    pub async fn zcachedb_dump_chunks(&self) {
        self.iter_chunks()
            .for_each(|(chunk, location)| async move {
                println!("{:?} from {:?}", chunk.id, location);
            })
            .await;
    }
}

impl<T: BlockBasedLogEntry> BlockBasedLogWithSummary<T> {
    pub async fn open(
        block_access: Arc<BlockAccess>,
        extent_allocator: Arc<ExtentAllocator>,
        phys: BlockBasedLogWithSummaryPhys<T>,
    ) -> BlockBasedLogWithSummary<T> {
        let chunk_summary = BlockBasedLog::open(
            block_access.clone(),
            extent_allocator.clone(),
            phys.chunk_summary,
        );

        // load in summary from disk
        let begin = Instant::now();
        let chunks = chunk_summary.iter().collect::<Vec<_>>().await;
        info!(
            "loaded summary of {} chunks ({}KB) in {}ms",
            chunks.len(),
            chunk_summary.num_bytes() / 1024,
            begin.elapsed().as_millis()
        );

        BlockBasedLogWithSummary {
            this: BlockBasedLog::open(block_access.clone(), extent_allocator.clone(), phys.this),
            chunk_summary,
            chunks,
            last_entry: phys.last_entry,
        }
    }

    pub async fn flush(&mut self) -> BlockBasedLogWithSummaryPhys<T> {
        let chunks = &mut self.chunks;
        let chunk_summary = &mut self.chunk_summary;
        self.this
            .flush_impl(|chunk_id, offset, first_entry| {
                assert_eq!(ChunkId(chunks.len() as u64), chunk_id);
                let entry = BlockBasedLogChunkSummaryEntry {
                    offset,
                    first_entry,
                };
                chunks.push(entry);
                chunk_summary.append(entry);
            })
            .await;
        // Note: it would be possible to redesign flush_impl() such that it did
        // all the "real" work and then returned a future that would just wait
        // for the i/o to complete.  Then we could be writing to disk both
        // "this" and the summary at the same time.
        let new_this = self.this.flush().await;
        let new_chunk_summary = self.chunk_summary.flush().await;

        BlockBasedLogWithSummaryPhys {
            this: new_this,
            chunk_summary: new_chunk_summary,
            last_entry: self.last_entry,
        }
    }

    /// Works only if there are no pending entries.
    /// Use flush() to retrieve the phys when there are pending entries.
    pub fn get_phys(&self) -> BlockBasedLogWithSummaryPhys<T> {
        assert!(self.this.pending_entries.is_empty());
        assert!(self.chunk_summary.pending_entries.is_empty());
        BlockBasedLogWithSummaryPhys {
            this: self.this.phys.clone(),
            chunk_summary: self.chunk_summary.phys.clone(),
            last_entry: self.last_entry,
        }
    }

    pub fn len(&self) -> u64 {
        self.this.len()
    }

    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        self.this.is_empty()
    }

    /// Size of the on-disk representation
    pub fn num_bytes(&self) -> u64 {
        self.this.num_bytes() + self.chunk_summary.num_bytes()
    }

    pub fn append(&mut self, entry: T) {
        self.last_entry = Some(entry);
        self.this.append(entry);
    }

    pub fn clear(&mut self) {
        self.last_entry = None;
        self.this.clear();
        self.chunk_summary.clear();
    }

    /// Iterates the on-disk state; panics if there are pending changes.
    pub fn iter(&self) -> impl Stream<Item = T> {
        self.this.iter()
    }

    /// Returns the exact location/size of this chunk (not the whole contiguous extent)
    fn chunk_extent(&self, chunk_id: usize) -> Extent {
        let chunk_summary = self.chunks[chunk_id];
        let chunk_size = if chunk_id == self.chunks.len() - 1 {
            self.this.phys.next_chunk_offset - chunk_summary.offset
        } else {
            self.chunks[chunk_id + 1].offset - chunk_summary.offset
        };

        let (extent_offset, extent) = self
            .this
            .phys
            .extents
            .range((Unbounded, Included(chunk_summary.offset)))
            .next_back()
            .unwrap();
        extent.range(chunk_summary.offset - *extent_offset, chunk_size)
    }

    async fn lookup_by_key_impl<B, F>(&self, key: &B, mut f: F) -> Option<T>
    where
        B: Ord + Debug,
        F: FnMut(&T) -> B,
    {
        assert_eq!(ChunkId(self.chunks.len() as u64), self.this.phys.next_chunk);

        // Check if the key is after the last entry.
        // XXX Note that this won't be very useful when there are multiple
        // pools.  When doing writes to all but the "last" pool, this check will
        // fail, and we'll have to read from the index for every write.  We
        // could address this by having one index (BlockBasedLogWithSummary) per
        // pool, or by replacing the last_entry optimization with a small cache
        // of BlockBasedLogChunk's.
        match self.last_entry {
            Some(last_entry) => {
                if key > &f(&last_entry) {
                    assert_gt!(key, &f(&self.chunks.last().unwrap().first_entry));
                    return None;
                }
            }
            None => {
                assert!(self.chunks.is_empty());
                return None;
            }
        }

        // Find the chunk_id that this key belongs in.
        let chunk_id = match self
            .chunks
            .binary_search_by_key(key, |chunk_summary| f(&chunk_summary.first_entry))
        {
            Ok(index) => index,
            Err(index) if index == 0 => return None, // key is before the first chunk, therefore not present
            Err(index) => index - 1,
        };

        // Read the chunk from disk.
        let chunk_extent = self.chunk_extent(chunk_id);
        trace!(
            "reading log chunk {} at {:?} to lookup {:?}",
            chunk_id,
            chunk_extent,
            key
        );
        let chunk_bytes = self.this.block_access.read_raw(chunk_extent).await;
        let (chunk, _consumed): (BlockBasedLogChunk<T>, usize) =
            self.this.block_access.chunk_from_raw(&chunk_bytes).unwrap();
        assert_eq!(chunk.id, ChunkId(chunk_id as u64));

        // XXX Can we assert that we are looking in the right chunk?  I think
        // we'd need the chunk to have the next chunk's first key as well.

        // Search within this chunk.
        match chunk.entries.binary_search_by_key(key, f) {
            Ok(index) => Some(chunk.entries[index]),
            Err(_) => None,
        }
    }

    /// Entries must have been added in sorted order, according to the provided
    /// key-extraction function.  Similar to Vec::binary_search_by_key().  The
    /// Guard returned helps the caller ensure that the Entry doesn't live
    /// longer than the reference on the Log (however, since the Entry is Copy,
    /// the caller still needs to be careful to not copy it, then drop the Log,
    /// allowing the Log to be modified before using the copy of the Entry).
    pub async fn lookup_by_key<B, F>(&self, key: &B, f: F) -> Option<BlockBasedLogValueGuard<'_, T>>
    where
        B: Ord + Debug,
        F: FnMut(&T) -> B,
    {
        let value = self.lookup_by_key_impl(key, f).await;
        value.map(|v| BlockBasedLogValueGuard {
            inner: v,
            _marker: &PhantomData,
        })
    }
}

pub struct BlockBasedLogValueGuard<'a, T: BlockBasedLogEntry> {
    inner: T,
    _marker: &'a PhantomData<T>,
}

impl<'a, T: BlockBasedLogEntry> std::ops::Deref for BlockBasedLogValueGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

#[derive(Serialize, Deserialize, Default, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct LogOffset(u64);

impl Add<usize> for LogOffset {
    type Output = LogOffset;

    fn add(self, rhs: usize) -> Self::Output {
        LogOffset(self.0 + rhs as u64)
    }
}
impl Sub<LogOffset> for LogOffset {
    type Output = u64;

    fn sub(self, rhs: LogOffset) -> Self::Output {
        self.0 - rhs.0
    }
}

#[derive(Serialize, Deserialize, Default, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct ChunkId(u64);
impl ChunkId {
    pub fn next(&self) -> ChunkId {
        ChunkId(self.0 + 1)
    }
}

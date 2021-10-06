use crate::base_types::*;
use crate::block_access::*;
use crate::block_based_log::*;
use crate::extent_allocator::ExtentAllocator;
use crate::zettacache::AtimeHistogramPhys;
use more_asserts::*;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd, Hash)]
pub struct IndexKey {
    pub guid: PoolGuid,
    pub block: BlockId,
}

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct IndexValue {
    pub location: DiskLocation,
    // XXX remove this and figure out based on which slab it's in?  However,
    // currently we need to return the right buffer size to the kernel, and it
    // isn't passing us the expected read size.  So we need to change some
    // interfaces to make that work right.
    pub size: u32,
    pub atime: Atime,
}

impl IndexValue {
    pub fn extent(&self) -> Extent {
        Extent {
            location: self.location,
            size: u64::from(self.size),
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Copy, Clone)]
pub struct IndexEntry {
    pub key: IndexKey,
    pub value: IndexValue,
}
impl OnDisk for IndexEntry {}
impl BlockBasedLogEntry for IndexEntry {}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
pub struct ZettaCacheIndexPhys {
    last_key: Option<IndexKey>,
    atime_histogram: AtimeHistogramPhys,
    log: BlockBasedLogWithSummaryPhys<IndexEntry>,
}

impl ZettaCacheIndexPhys {
    pub fn new(min_atime: Atime) -> Self {
        Self {
            last_key: None,
            atime_histogram: AtimeHistogramPhys::new(min_atime),
            log: Default::default(),
        }
    }
}

pub struct ZettaCacheIndex {
    pub last_key: Option<IndexKey>,
    pub atime_histogram: AtimeHistogramPhys,
    pub log: BlockBasedLogWithSummary<IndexEntry>,
}

impl std::fmt::Debug for ZettaCacheIndex {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ZettaCacheIndex")
            .field("last_key", &self.last_key)
            .finish()
    }
}

impl ZettaCacheIndex {
    pub async fn open(
        block_access: Arc<BlockAccess>,
        extent_allocator: Arc<ExtentAllocator>,
        phys: ZettaCacheIndexPhys,
    ) -> Self {
        Self {
            last_key: phys.last_key,
            atime_histogram: phys.atime_histogram,
            log: BlockBasedLogWithSummary::open(block_access, extent_allocator, phys.log).await,
        }
    }

    pub async fn flush(&mut self) -> ZettaCacheIndexPhys {
        ZettaCacheIndexPhys {
            last_key: self.last_key,
            atime_histogram: self.atime_histogram.clone(),
            log: self.log.flush().await,
        }
    }

    /// Retrieve the index phys. This only works if there are no pending log entries.
    /// Use flush() to retrieve the phys when there are pending entries.
    pub fn get_phys(&self) -> ZettaCacheIndexPhys {
        ZettaCacheIndexPhys {
            last_key: self.last_key,
            atime_histogram: self.atime_histogram.clone(),
            log: self.log.get_phys(),
        }
    }

    pub fn first_atime(&self) -> Atime {
        self.atime_histogram.first()
    }

    pub fn update_last_key(&mut self, key: IndexKey) {
        if let Some(last_key) = self.last_key {
            assert_gt!(key, last_key);
        }
        self.last_key = Some(key);
    }

    pub fn append(&mut self, entry: IndexEntry) {
        self.update_last_key(entry.key);
        self.atime_histogram.insert(entry.value);
        self.log.append(entry);
    }

    pub fn clear(&mut self) {
        self.last_key = None;
        self.atime_histogram.clear();
        self.log.clear();
    }
}

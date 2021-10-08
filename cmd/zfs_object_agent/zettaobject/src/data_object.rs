use crate::{base_types::*, ObjectAccess};
use anyhow::{Context, Result};
use log::*;
use more_asserts::*;
use serde::{Deserialize, Serialize};
use serde_bytes::ByteBuf;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fmt;
use std::fmt::Display;
use std::time::Instant;
use zettacache::base_types::*;

#[derive(Serialize, Deserialize, Debug)]
pub struct DataObjectPhys {
    pub guid: PoolGuid,      // redundant with key, for verification
    pub object: ObjectId,    // redundant with key, for verification
    pub blocks_size: u32,    // sum of blocks.values().len()
    pub min_block: BlockId,  // inclusive (all blocks are >= min_block)
    pub next_block: BlockId, // exclusive (all blocks are < next_block)

    // Note: if this object was rewritten to consolidate adjacent objects, the
    // blocks in this object may have been originally written over a range of
    // TXG's.
    pub min_txg: Txg,
    pub max_txg: Txg, // inclusive

    pub blocks: HashMap<BlockId, ByteBuf>,
}
impl OnDisk for DataObjectPhys {}

impl Display for DataObjectPhys {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:?}: blocks={} bytes={} BlockId[{},{}) TXG[{},{}]",
            self.object,
            self.blocks.len(),
            self.blocks_size,
            self.min_block,
            self.next_block,
            self.min_txg.0,
            self.max_txg.0,
        )
    }
}

const NUM_DATA_PREFIXES: u64 = 64;

impl DataObjectPhys {
    pub fn new(guid: PoolGuid, object: ObjectId, next_block: BlockId, txg: Txg) -> Self {
        DataObjectPhys {
            guid,
            object,
            min_block: next_block,
            next_block,
            min_txg: txg,
            max_txg: txg,
            blocks_size: 0,
            blocks: HashMap::new(),
        }
    }

    pub fn key(guid: PoolGuid, object: ObjectId) -> String {
        format!(
            "zfs/{}/data/{:03}/{}",
            guid,
            object.0 % NUM_DATA_PREFIXES,
            object
        )
    }

    // Could change this to return an Iterator
    pub fn prefixes(guid: PoolGuid) -> Vec<String> {
        let mut vec = Vec::new();
        for x in 0..NUM_DATA_PREFIXES {
            vec.push(format!("zfs/{}/data/{:03}/", guid, x));
        }
        vec
    }

    pub fn calculate_blocks_size(&self) -> u32 {
        self.blocks
            .values()
            .map(|block| u32::try_from(block.len()).unwrap())
            .sum()
    }

    fn verify(&self) {
        assert_eq!(self.blocks_size, self.calculate_blocks_size());
        assert_le!(self.min_txg, self.max_txg);
        assert_le!(self.min_block, self.next_block);
        if !self.blocks.is_empty() {
            assert_le!(self.min_block, self.blocks.keys().min().unwrap());
            assert_gt!(self.next_block, self.blocks.keys().max().unwrap());
        }
    }

    pub async fn get(
        object_access: &ObjectAccess,
        guid: PoolGuid,
        object: ObjectId,
        bypass_cache: bool,
    ) -> Result<Self> {
        let buf = match bypass_cache {
            true => {
                object_access
                    .get_object_uncached(Self::key(guid, object))
                    .await?
            }
            false => object_access.get_object(Self::key(guid, object)).await?,
        };
        let begin = Instant::now();
        let this: DataObjectPhys = bincode::deserialize(&buf)
            .with_context(|| format!("Failed to decode contents of {}", Self::key(guid, object)))?;
        trace!(
            "{:?}: deserialized {} blocks from {} bytes in {}ms",
            this.object,
            this.blocks.len(),
            buf.len(),
            begin.elapsed().as_millis()
        );
        assert_eq!(this.guid, guid);
        assert_eq!(this.object, object);
        this.verify();
        Ok(this)
    }

    pub async fn get_from_key(
        object_access: &ObjectAccess,
        key: String,
        bypass_cache: bool,
    ) -> Result<Self> {
        let buf = match bypass_cache {
            true => object_access.get_object_uncached(key.clone()).await?,
            false => object_access.get_object(key.clone()).await?,
        };
        let begin = Instant::now();
        let this: DataObjectPhys = bincode::deserialize(&buf)
            .with_context(|| format!("Failed to decode contents of {}", key))?;
        trace!(
            "{:?}: deserialized {} blocks from {} bytes in {}ms",
            this.object,
            this.blocks.len(),
            buf.len(),
            begin.elapsed().as_millis()
        );
        this.verify();
        Ok(this)
    }

    pub async fn put(&self, object_access: &ObjectAccess) {
        let begin = Instant::now();
        let contents = bincode::serialize(self).unwrap();
        trace!(
            "{:?}: serialized {} blocks in {} bytes in {}ms",
            self.object,
            self.blocks.len(),
            contents.len(),
            begin.elapsed().as_millis()
        );
        self.verify();
        object_access
            .put_object(Self::key(self.guid, self.object), contents)
            .await;
    }

    pub fn get_block(&self, block: BlockId) -> &[u8] {
        match self.blocks.get(&block) {
            Some(buf) => buf,
            None => {
                panic!("{:?} not found in {:?}: {:?}", block, self.object, self);
            }
        }
    }

    pub fn blocks_len(&self) -> u32 {
        u32::try_from(self.blocks.len()).unwrap()
    }

    pub fn is_empty(&self) -> bool {
        self.blocks.is_empty()
    }
}

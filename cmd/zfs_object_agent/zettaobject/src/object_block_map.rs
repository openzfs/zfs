use crate::base_types::*;
use crate::object_based_log::ObjectBasedLog;
use crate::object_based_log::ObjectBasedLogEntry;
use futures::future;
use futures::StreamExt;
use log::*;
use more_asserts::*;
use serde::{Deserialize, Serialize};
use std::borrow::Borrow;
use std::collections::BTreeSet;
use std::ops::Bound::*;
use std::sync::RwLock;
use std::time::Instant;
use zettacache::base_types::*;

#[derive(Debug, Serialize, Deserialize, Copy, Clone)]
// XXX make this private and make methods for everything that uses it
pub enum StorageObjectLogEntry {
    Alloc {
        object: ObjectId,
        min_block: BlockId,
    },
    Free {
        object: ObjectId,
    },
}
impl OnDisk for StorageObjectLogEntry {}
impl ObjectBasedLogEntry for StorageObjectLogEntry {}

#[derive(Debug)]
pub struct ObjectBlockMap {
    state: RwLock<ObjectBlockMapState>,
}

#[derive(Debug)]
struct ObjectBlockMapState {
    map: BTreeSet<ObjectBlockMapEntry>,
    next_block: BlockId,
}

#[derive(Debug, Ord, PartialOrd, PartialEq, Eq, Copy, Clone)]
pub struct ObjectBlockMapEntry {
    pub object: ObjectId,
    pub block: BlockId,
}

impl Borrow<ObjectId> for ObjectBlockMapEntry {
    fn borrow(&self) -> &ObjectId {
        &self.object
    }
}

impl Borrow<BlockId> for ObjectBlockMapEntry {
    fn borrow(&self) -> &BlockId {
        &self.block
    }
}

impl ObjectBlockMap {
    pub async fn load(
        storage_object_log: &ObjectBasedLog<StorageObjectLogEntry>,
        next_block: BlockId,
    ) -> Self {
        let begin = Instant::now();
        let mut num_alloc_entries: u64 = 0;
        let mut num_free_entries: u64 = 0;
        let mut map: BTreeSet<ObjectBlockMapEntry> = BTreeSet::new();
        storage_object_log
            .iterate()
            .for_each(|ent| {
                match ent {
                    StorageObjectLogEntry::Alloc {
                        object,
                        min_block: first_possible_block,
                    } => {
                        Self::insert_setup(&mut map, object, first_possible_block);
                        num_alloc_entries += 1;
                    }
                    StorageObjectLogEntry::Free { object } => {
                        let removed = map.remove(&object);
                        assert!(removed);
                        num_free_entries += 1;
                    }
                }

                future::ready(())
            })
            .await;
        info!(
            "loaded mapping from {} objects with {} allocs and {} frees in {}ms",
            storage_object_log.num_chunks,
            num_alloc_entries,
            num_free_entries,
            begin.elapsed().as_millis()
        );

        let mut prev_ent_opt: Option<ObjectBlockMapEntry> = None;
        for ent in map.iter() {
            if let Some(prev_ent) = prev_ent_opt {
                assert_gt!(ent.object, prev_ent.object);
                assert_gt!(ent.block, prev_ent.block);
            }
            prev_ent_opt = Some(*ent);
        }
        ObjectBlockMap {
            state: RwLock::new(ObjectBlockMapState { map, next_block }),
        }
    }

    // during setup (i.e. before done_setting_up() is called), we can insert in
    // any order, and without specifying the next_block
    pub fn insert_setup(
        map: &mut BTreeSet<ObjectBlockMapEntry>,
        object: ObjectId,
        first_block: BlockId,
    ) {
        // verify that this block is between the existing entries blocks
        let prev_ent_opt = map.range((Unbounded, Excluded(object))).next_back();
        if let Some(prev_ent) = prev_ent_opt {
            assert_lt!(prev_ent.block, first_block);
        }
        let next_ent_opt = map.range((Excluded(object), Unbounded)).next();
        if let Some(next_ent) = next_ent_opt {
            assert_gt!(next_ent.block, first_block);
        }
        // verify that this object is not yet in the map
        assert!(!map.contains(&object));

        map.insert(ObjectBlockMapEntry {
            object,
            block: first_block,
        });
    }

    pub fn insert(&self, object: ObjectId, first_block: BlockId, next_block: BlockId) {
        // verify that this object and block are after the last
        let mut state = self.state.write().unwrap();
        assert_lt!(first_block, next_block);
        assert_eq!(first_block, state.next_block);
        if let Some(last_ent) = state.map.iter().next_back() {
            assert_gt!(object, last_ent.object);
            assert_gt!(first_block, last_ent.block);
        }

        state.map.insert(ObjectBlockMapEntry {
            object,
            block: first_block,
        });

        state.next_block = next_block;
    }

    pub fn remove(&self, object: ObjectId) {
        let mut state = self.state.write().unwrap();
        let removed = state.map.remove(&object);
        assert!(removed);
    }

    pub fn block_to_object(&self, block: BlockId) -> ObjectId {
        let state = self.state.read().unwrap();
        assert_lt!(block, state.next_block);
        state
            .map
            .range((Unbounded, Included(block)))
            .next_back()
            .unwrap()
            .object
    }

    pub fn object_to_min_block(&self, object: ObjectId) -> BlockId {
        let state = self.state.read().unwrap();
        state.map.get(&object).unwrap().block
    }

    pub fn object_to_next_block(&self, object: ObjectId) -> BlockId {
        let state = self.state.read().unwrap();

        // The "next block" (i.e. the first BlockID that's not valid in this
        // object) is the next object's first block.  Or if this is the last
        // object, it's the next block of the entire pool (state.next_block).
        match state.map.range((Excluded(object), Unbounded)).next() {
            Some(entry) => entry.block,
            None => state.next_block,
        }
    }

    pub fn last_object(&self) -> ObjectId {
        let state = self.state.read().unwrap();
        state
            .map
            .iter()
            .next_back()
            .unwrap_or(&ObjectBlockMapEntry {
                object: ObjectId(0),
                block: BlockId(0),
            })
            .object
    }

    pub fn len(&self) -> usize {
        let state = self.state.read().unwrap();
        state.map.len()
    }

    #[allow(dead_code)]
    pub fn is_empty(&self) -> bool {
        let state = self.state.read().unwrap();
        state.map.is_empty()
    }

    pub fn for_each<F>(&self, mut f: F)
    where
        F: FnMut(&ObjectBlockMapEntry),
    {
        let state = self.state.read().unwrap();
        for ent in state.map.iter() {
            f(ent);
        }
    }
}

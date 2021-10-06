use crate::base_types::*;
use crate::object_access::ObjectAccess;
use crate::pool::PoolSharedState;
use anyhow::{Context, Result};
use async_stream::stream;
use futures::future::join_all;
use futures::future::{self, join};
use futures::stream::{FuturesOrdered, StreamExt};
use futures_core::Stream;
use lazy_static::lazy_static;
use log::*;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use std::marker::PhantomData;
use std::sync::Arc;
use std::time::Instant;
use tokio::task::JoinHandle;
use util::get_tunable;
use zettacache::base_types::*;

lazy_static! {
    pub static ref ENTRIES_PER_OBJECT: usize = get_tunable("entries_per_object", 100_000);
    pub static ref OBJECT_LOG_ITERATE_QUEUE_DEPTH: usize =
        get_tunable("object_log_iterate_queue_depth", 100);
}

/*
 * Note: The OBLIterator returns a struct, not a reference. That way it doesn't
 * have to manage the reference lifetime.  It also means that the ObjectBasedLog
 * needs to contain a Copy/Clone type so that we can copy it to return from the
 * OBLIterator.
 */
pub trait ObjectBasedLogEntry: 'static + OnDisk + Copy + Clone + Unpin + Send + Sync {}

#[derive(Serialize, Deserialize, Debug)]
pub struct ObjectBasedLogPhys<T: ObjectBasedLogEntry> {
    generation: u64,
    num_chunks: u64,
    num_entries: u64,
    key: String,
    entry_type: PhantomData<T>,
}

impl<T: ObjectBasedLogEntry> ObjectBasedLogPhys<T> {
    pub async fn cleanup_older_generations(&self, object_access: &ObjectAccess) {
        // Stream<Item=String> of generation prefixes
        let generations = object_access
            .list_prefixes(format!("{}/", self.key))
            .filter(|key| {
                future::ready(
                    key.rsplit('/').collect::<Vec<&str>>()[1]
                        .parse::<u64>()
                        .unwrap()
                        < self.generation,
                )
            })
            .inspect(|key| debug!("cleanup: old generation {}", key));

        object_access
            .delete_objects(
                generations
                    .flat_map(|generation| {
                        Box::pin(object_access.list_objects(generation, None, false))
                    })
                    .inspect(|key| trace!("cleanup: old generation chunk {}", key)),
            )
            .await;
    }
}

#[derive(Serialize, Deserialize, Debug)]
struct ObjectBasedLogChunk<T: ObjectBasedLogEntry> {
    guid: PoolGuid,
    generation: u64,
    chunk: u64,
    txg: Txg,
    #[serde(bound(deserialize = "Vec<T>: DeserializeOwned"))]
    entries: Vec<T>,
}
impl<T: ObjectBasedLogEntry> OnDisk for ObjectBasedLogChunk<T> {}

impl<T: ObjectBasedLogEntry> ObjectBasedLogChunk<T> {
    fn key(name: &str, generation: u64, chunk: u64) -> String {
        format!("{}/{:020}/{:020}", name, generation, chunk)
    }

    async fn get(
        object_access: &ObjectAccess,
        name: &str,
        generation: u64,
        chunk: u64,
    ) -> Result<Self> {
        let buf = object_access
            .get_object(Self::key(name, generation, chunk))
            .await?;
        let begin = Instant::now();
        let this: Self = serde_json::from_slice(&buf).with_context(|| {
            format!(
                "Failed to decode contents of {}",
                Self::key(name, generation, chunk)
            )
        })?;
        debug!(
            "deserialized {} log entries in {}ms",
            this.entries.len(),
            begin.elapsed().as_millis()
        );
        assert_eq!(this.generation, generation);
        assert_eq!(this.chunk, chunk);
        Ok(this)
    }

    async fn put(&self, object_access: &ObjectAccess, name: &str) {
        let begin = Instant::now();
        let buf = serde_json::to_vec(&self).unwrap();
        debug!(
            "serialized {} log entries in {}ms",
            self.entries.len(),
            begin.elapsed().as_millis()
        );
        object_access
            .put_object(Self::key(name, self.generation, self.chunk), buf)
            .await;
    }
}

//#[derive(Debug)]
pub struct ObjectBasedLog<T: ObjectBasedLogEntry> {
    shared_state: Arc<PoolSharedState>,
    name: String,
    generation: u64,
    num_flushed_chunks: u64,
    pub num_chunks: u64,
    pub num_entries: u64,
    pending_entries: Vec<T>,
    recovered: bool,
    pending_flushes: Vec<JoinHandle<()>>,
}

pub struct ObjectBasedLogRemainder {
    chunk: u64,
}

impl<T: ObjectBasedLogEntry> ObjectBasedLog<T> {
    pub fn create(shared_state: Arc<PoolSharedState>, name: &str) -> ObjectBasedLog<T> {
        ObjectBasedLog {
            shared_state,
            name: name.to_string(),
            generation: 0,
            num_flushed_chunks: 0,
            num_chunks: 0,
            num_entries: 0,
            recovered: true,
            pending_entries: Vec::new(),
            pending_flushes: Vec::new(),
        }
    }

    pub fn open_by_phys(
        shared_state: Arc<PoolSharedState>,
        phys: &ObjectBasedLogPhys<T>,
    ) -> ObjectBasedLog<T> {
        ObjectBasedLog {
            shared_state,
            name: phys.key.clone(),
            generation: phys.generation,
            num_flushed_chunks: phys.num_chunks,
            num_chunks: phys.num_chunks,
            num_entries: phys.num_entries,
            recovered: false,
            pending_entries: Vec::new(),
            pending_flushes: Vec::new(),
        }
    }

    /*
    pub fn verify_clean_shutdown(&mut self) {
        // Make sure there are no objects past the logical end of the log
        self.recovered = true;
    }
    */

    /// Return this log's parent prefix (e.g. zfs/15238822373695050151/PendingFreesLog)
    pub fn parent_prefix(&self) -> String {
        self.name.rsplitn(2, '/').last().unwrap().to_string()
    }

    /// Recover after a system crash, where the kernel also crashed and we are discarding
    /// any changes after the current txg.
    pub async fn cleanup(&mut self) {
        // collect chunks past the end, in the current generation
        let shared_state = self.shared_state.clone();
        let last_generation_key = format!("{}/{:020}/", self.name, self.generation);
        let start_after = if self.num_chunks == 0 {
            None
        } else {
            Some(ObjectBasedLogChunk::<T>::key(
                &self.name,
                self.generation,
                self.num_chunks - 1,
            ))
        };
        let current_generation_cleanup = async move {
            shared_state
                .object_access
                .delete_objects(
                    shared_state
                        .object_access
                        .list_objects(last_generation_key, start_after, true)
                        .inspect(|key| {
                            info!(
                                "cleanup: deleting future chunk of current generation: {}",
                                key
                            )
                        }),
                )
                .await;
        };

        // collect chunks from the partially-complete future generation
        let shared_state = self.shared_state.clone();
        let next_generation_key = format!("{}/{:020}/", self.name, self.generation + 1);
        let next_generation_cleanup = async move {
            shared_state
                .object_access
                .delete_objects(
                    shared_state
                        .object_access
                        .list_objects(next_generation_key, None, true)
                        .inspect(|key| {
                            info!("cleanup: deleting chunk of future generation: {}", key)
                        }),
                )
                .await;
        };

        // execute both cleanup's concurrently
        join(current_generation_cleanup, next_generation_cleanup).await;

        self.recovered = true;
    }

    pub fn to_phys(&self) -> ObjectBasedLogPhys<T> {
        ObjectBasedLogPhys {
            generation: self.generation,
            num_chunks: self.num_chunks,
            num_entries: self.num_entries,
            key: self.name.clone(),
            entry_type: PhantomData,
        }
    }

    pub fn append(&mut self, txg: Txg, entry: T) {
        assert!(self.recovered);
        // XXX assert that txg is the same as the txg for the other pending entries?
        self.pending_entries.push(entry);
        // XXX should be based on chunk size (bytes)?  Or maybe should just be unlimited.
        if self.pending_entries.len() > *ENTRIES_PER_OBJECT {
            self.initiate_flush(txg);
        }
    }

    pub fn initiate_flush(&mut self, txg: Txg) {
        assert!(self.recovered);

        let chunk = ObjectBasedLogChunk {
            guid: self.shared_state.guid,
            txg,
            generation: self.generation,
            chunk: self.num_chunks,
            entries: self.pending_entries.split_off(0),
        };

        self.num_chunks += 1;
        self.num_entries += chunk.entries.len() as u64;

        // XXX cloning name, would be nice if we could find a way to
        // reference them from the spawned task (use Arc)
        let shared_state = self.shared_state.clone();
        let name = self.name.clone();
        let handle = tokio::spawn(async move {
            chunk.put(&shared_state.object_access, &name).await;
        });
        self.pending_flushes.push(handle);

        assert!(self.pending_entries.is_empty());
    }

    pub async fn flush(&mut self, txg: Txg) {
        if !self.pending_entries.is_empty() {
            self.initiate_flush(txg);
        }
        let wait_for = self.pending_flushes.split_off(0);
        let join_result = join_all(wait_for).await;
        for r in join_result {
            r.unwrap();
        }
        self.num_flushed_chunks = self.num_chunks;
    }

    pub async fn clear(&mut self, txg: Txg) {
        self.flush(txg).await;
        self.generation += 1;
        self.num_chunks = 0;
        self.num_entries = 0;
    }

    /// Iterates the on-disk state; panics if there are pending changes.
    pub fn iterate(&self) -> impl Stream<Item = T> {
        assert_eq!(self.num_flushed_chunks, self.num_chunks);
        assert!(self.pending_entries.is_empty());
        assert!(self.pending_flushes.is_empty());
        self.iter_most().0
    }

    /// Iterates on-disk state, returns (stream, next_chunk), where the
    /// next_chunk can be passed in to a subsequent call to iterate the later
    /// entries that were not iterated by this stream
    fn iter_impl(
        &self,
        first_chunk_opt: Option<ObjectBasedLogRemainder>,
    ) -> (impl Stream<Item = T>, ObjectBasedLogRemainder) {
        let mut stream = FuturesOrdered::new();
        let generation = self.generation;
        let first_chunk = match first_chunk_opt {
            Some(rem) => rem.chunk,
            None => 0,
        };
        for chunk in first_chunk..self.num_flushed_chunks {
            let shared_state = self.shared_state.clone();
            let n = self.name.clone();
            stream.push(future::ready(async move {
                ObjectBasedLogChunk::get(&shared_state.object_access, &n, generation, chunk)
                    .await
                    .unwrap()
            }));
        }
        // Note: buffered() is needed because rust-s3 creates one connection for
        // each request, rather than using a connection pool. If we created 1000
        // connections we'd run into the open file descriptor limit.
        let mut buffered_stream = stream.buffered(*OBJECT_LOG_ITERATE_QUEUE_DEPTH);
        (
            stream! {
                while let Some(chunk) = buffered_stream.next().await {
                    trace!("yielding entries of chunk {}", chunk.chunk);
                    for ent in chunk.entries {
                        yield ent;
                    }
                }
            },
            ObjectBasedLogRemainder {
                chunk: self.num_flushed_chunks,
            },
        )
    }

    /// Iterates the on-disk state; pending changes (including pending_entries
    /// and pending_flushes) will not be visited.  Returns token for iterating
    /// the remainder (entries after those visited here).
    pub fn iter_most(&self) -> (impl Stream<Item = T>, ObjectBasedLogRemainder) {
        self.iter_impl(None)
    }

    /// Iterates over the remainder of the log, starting from the token.  Waits
    /// (async) for any pending changes to be flushed.
    pub async fn iter_remainder(
        &mut self,
        txg: Txg,
        first_chunk: ObjectBasedLogRemainder,
    ) -> impl Stream<Item = T> {
        self.flush(txg).await;
        // XXX It would be faster if we kept all the "remainder" entries in RAM
        // until we iter the remainder and transfer it to the new generation.
        self.iter_impl(Some(first_chunk)).0
    }
}

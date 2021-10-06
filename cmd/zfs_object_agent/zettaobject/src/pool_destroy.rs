use crate::object_access::ObjectAccess;
use crate::object_access::OBJECT_DELETION_BATCH_SIZE;
use crate::pool::PoolPhys;
use anyhow::anyhow;
use anyhow::Result;
use futures::stream::StreamExt;
use lazy_static::lazy_static;
use log::*;
use nvpair::NvList;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::io::ErrorKind;
use std::process;
use std::time::SystemTime;
use tokio::fs;
use tokio::fs::OpenOptions;
use tokio::io::AsyncReadExt;
use tokio::sync::mpsc;
use tokio::sync::Mutex;
use util::get_tunable;
use zettacache::base_types::*;

lazy_static! {
    static ref POOL_DESTROYER: Mutex<Option<PoolDestroyer>> = Default::default();

    // Persist zpool destroy progress after this number of iterations of bulk object destroy.
    static ref DESTROY_PROGRESS_FREQUENCY: usize = get_tunable("destroy_progress_frequency", 10);
}

#[derive(Serialize, Deserialize, Debug, Clone, Copy)]
pub struct PoolDestroyingPhys {
    start_time: SystemTime,
    total_data_objects: u64,
    destroyed_objects: u64,
}
impl OnDisk for PoolDestroyingPhys {}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
enum PoolDestroyState {
    InProgress,
    Complete,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
struct DestroyingCacheItemPhys {
    name: String,
    endpoint: String,
    region: String,
    bucket: String,
    profile: Option<String>,
    state: PoolDestroyState,
}

#[derive(Serialize, Deserialize, Default, Debug)]
struct DestroyingCachePhys {
    pools: HashMap<PoolGuid, DestroyingCacheItemPhys>,
}

#[derive(Debug)]
struct DestroyingPool {
    cache_phys: DestroyingCacheItemPhys,
    destroying_phys: Option<PoolDestroyingPhys>,
}

#[derive(Default, Debug)]
struct DestroyingPoolsMap {
    pools: HashMap<PoolGuid, DestroyingPool>,
}

impl DestroyingPoolsMap {
    fn from_phys(destroying_cache_phys: DestroyingCachePhys) -> Self {
        let pools = destroying_cache_phys
            .pools
            .iter()
            .map(|(guid, phys)| {
                (
                    *guid,
                    DestroyingPool {
                        cache_phys: phys.clone(),
                        destroying_phys: None,
                    },
                )
            })
            .collect::<HashMap<PoolGuid, DestroyingPool>>();

        DestroyingPoolsMap { pools }
    }

    fn to_phys(&self) -> DestroyingCachePhys {
        DestroyingCachePhys {
            pools: self
                .pools
                .iter()
                .map(|(guid, item)| (*guid, item.cache_phys.clone()))
                .collect(),
        }
    }

    fn increment_destroyed_objects(&mut self, guid: PoolGuid, delta: u64) {
        let destroying_pool = self.pools.get_mut(&guid).unwrap();
        let destroying_phys = destroying_pool.destroying_phys.as_mut().unwrap();
        destroying_phys.destroyed_objects += delta;

        trace!(
            "Updating zpool destroy list: guid={} total objects={} destroyed_objects={}.",
            &guid,
            destroying_phys.total_data_objects,
            destroying_phys.destroyed_objects
        );
    }

    fn update_destroy_complete(&mut self, guid: PoolGuid) {
        let destroying_pool = self.pools.get_mut(&guid).unwrap();
        destroying_pool.cache_phys.state = PoolDestroyState::Complete;

        debug!(
            "update_destroy_complete: guid={} total objects={}.",
            &guid,
            destroying_pool.destroying_phys.unwrap().total_data_objects
        );
    }

    async fn remove_not_in_progress(&mut self) {
        debug!("Clearing destroyed pools.");
        {
            self.pools.retain(|_, destroying_pool| {
                destroying_pool.cache_phys.state == PoolDestroyState::InProgress
            });
        }
    }

    fn to_nvlist(&self) -> NvList {
        let mut nvl = NvList::new_unique_names();

        for (guid, destroying_pool) in self.pools.iter() {
            let mut nvl_item = NvList::new_unique_names();
            nvl_item.insert("GUID", &guid.0).unwrap();
            nvl_item
                .insert("name", destroying_pool.cache_phys.name.as_str())
                .unwrap();
            nvl_item
                .insert("endpoint", destroying_pool.cache_phys.endpoint.as_str())
                .unwrap();
            nvl_item
                .insert("region", destroying_pool.cache_phys.region.as_str())
                .unwrap();
            nvl_item
                .insert("bucket", destroying_pool.cache_phys.bucket.as_str())
                .unwrap();
            nvl_item
                .insert(
                    "destroy_completed",
                    &(destroying_pool.cache_phys.state == PoolDestroyState::Complete),
                )
                .unwrap();

            // When the destroy task is initializing, there could be a short period when
            // destroying_phys has not been initialized. Skip the destroying_phys
            // if it is None.
            if let Some(destroying_phys) = destroying_pool.destroying_phys {
                nvl_item
                    .insert(
                        "start_time",
                        &destroying_phys
                            .start_time
                            .duration_since(std::time::UNIX_EPOCH)
                            .unwrap()
                            .as_secs(),
                    )
                    .unwrap();
                nvl_item
                    .insert("total_data_objects", &destroying_phys.total_data_objects)
                    .unwrap();
                nvl_item
                    .insert("destroyed_objects", &destroying_phys.destroyed_objects)
                    .unwrap();
            }

            nvl.insert(format!("{}", guid), nvl_item.as_ref()).unwrap();
        }

        nvl
    }
}

#[derive(Default, Debug)]
struct PoolDestroyer {
    destroying_pools_map: DestroyingPoolsMap,

    // Full path to the zpool-destroy cache file, initialized at startup.
    destroy_cache_filename: String,
}

impl PoolDestroyer {
    async fn init(&mut self) -> Result<()> {
        match OpenOptions::new()
            .read(true)
            .open(&self.destroy_cache_filename)
            .await
        {
            Ok(mut cache_file) => {
                let mut buffer = String::new();
                cache_file.read_to_string(&mut buffer).await?;
                if !buffer.is_empty() {
                    self.destroying_pools_map =
                        DestroyingPoolsMap::from_phys(serde_json::from_str(&buffer)?);
                }

                // Fire off destroy tasks
                self.destroying_pools_map
                    .pools
                    .iter()
                    .filter(|(_, destroying_pool)| {
                        destroying_pool.cache_phys.state == PoolDestroyState::InProgress
                    })
                    .for_each(|(guid, destroying_pool)| {
                        let object_access = ObjectAccess::new(
                            &destroying_pool.cache_phys.endpoint,
                            &destroying_pool.cache_phys.region,
                            &destroying_pool.cache_phys.bucket,
                            destroying_pool.cache_phys.profile.clone(),
                            false,
                        );

                        start_destroy_task(object_access, *guid);
                    });

                Ok(())
            }
            Err(ref error) if error.kind() == ErrorKind::NotFound => {
                // zpool_destroy.cache file does not exist. No initialization is needed.
                info!("{} does not exist", &self.destroy_cache_filename);
                Ok(())
            }
            Err(error) => Err(anyhow!(
                "Error opening {}; {:?}",
                &self.destroy_cache_filename,
                error
            )),
        }
    }

    /// Write out the DestroyingPoolsMap as json so that if the agent restarts, it can continue
    /// destroying the pools that were in the process of being destroyed. To ensure that an agent
    /// crash does not leave the cache file partially written, we always write to a temp file and
    /// rename it atomically to replace the cache file.
    async fn write(&self) -> Result<()> {
        trace!("Writing out destroy cache file.");

        let temp_filename = format!("{}.{}", &self.destroy_cache_filename, process::id());

        fs::write(
            &temp_filename,
            serde_json::to_string_pretty(&self.destroying_pools_map.to_phys())
                .unwrap()
                .as_bytes(),
        )
        .await?;

        fs::rename(&temp_filename, &self.destroy_cache_filename).await?;

        Ok(())
    }

    /// Mark a pool as destroyed by writing to its super object.
    async fn mark_pool_destroying(
        &mut self,
        object_access: &ObjectAccess,
        guid: PoolGuid,
        total_data_objects: u64,
    ) {
        // Get super object and mark the pool as destroyed.
        let mut pool_phys = PoolPhys::get(object_access, guid).await.unwrap();
        assert!(pool_phys.destroying_state.is_none());

        // write super marking the pool as destroyed.
        pool_phys.destroying_state = Some(PoolDestroyingPhys {
            start_time: SystemTime::now(),
            total_data_objects,
            destroyed_objects: 0,
        });
        pool_phys.put(object_access).await;
    }

    /// Write to zpool_destroy.cache file so that we can resume destroy if the agent restarts.
    async fn add_zpool_destroy_cache(
        &mut self,
        object_access: &ObjectAccess,
        guid: PoolGuid,
    ) -> Result<()> {
        let pool_phys = PoolPhys::get(object_access, guid).await?;
        let destroyed_pool = pool_phys.destroying_state.unwrap();

        let destroying_pool = DestroyingPool {
            cache_phys: DestroyingCacheItemPhys {
                name: pool_phys.name,
                endpoint: object_access.endpoint(),
                region: object_access.region(),
                profile: object_access.credentials_profile(),
                bucket: object_access.bucket(),
                state: PoolDestroyState::InProgress,
            },
            destroying_phys: Some(PoolDestroyingPhys {
                start_time: destroyed_pool.start_time,
                total_data_objects: destroyed_pool.total_data_objects,
                destroyed_objects: destroyed_pool.destroyed_objects,
            }),
        };

        info!(
            "marking pool {} destroyed; total_data_objects: {} destroyed_objects: {}",
            guid, destroyed_pool.total_data_objects, destroyed_pool.destroyed_objects
        );

        if self.destroying_pools_map.pools.contains_key(&guid) {
            return Err(anyhow!("pool {} already in destroying_pools_map", guid));
        }
        self.destroying_pools_map
            .pools
            .insert(guid, destroying_pool);

        self.write().await?;

        Ok(())
    }
}

fn delete_pool_objects(
    object_access: &ObjectAccess,
    guid: PoolGuid,
    sender: tokio::sync::mpsc::UnboundedSender<usize>,
) {
    let oa: ObjectAccess = object_access.clone();

    tokio::spawn(async move {
        let prefix = format!("zfs/{}/", guid);
        let super_object = PoolPhys::key(guid);
        let batch_size = *OBJECT_DELETION_BATCH_SIZE * *DESTROY_PROGRESS_FREQUENCY;
        let mut count = 0;

        oa.delete_objects(
            oa.list_objects(prefix, None, false)
                // Skip the super object as we use it to track the progress made by this task.
                .filter(|o| futures::future::ready(super_object.ne(o)))
                .inspect(|_| {
                    // Note: We are counting the objects listed rather than the objects deleted.
                    // This works as we are fine with not being very accurate.
                    count += 1;
                    if count >= batch_size {
                        sender.send(count).unwrap();
                        count = 0;
                    }
                }),
        )
        .await;
    });
}

async fn destroy_task(object_access: ObjectAccess, guid: PoolGuid) {
    info!("destroying pool {}", guid);

    // There can only be one destroy task for any given pool and
    let mut pool_phys = PoolPhys::get(&object_access, guid).await.unwrap();
    POOL_DESTROYER
        .lock()
        .await
        .as_mut()
        .unwrap()
        .destroying_pools_map
        .pools
        .get_mut(&guid)
        .unwrap()
        .destroying_phys = pool_phys.destroying_state;

    let (tx, mut rx) = mpsc::unbounded_channel();
    delete_pool_objects(&object_access, guid, tx);

    // Wait for the delete_prefix task to send progress updates.
    while let Some(object_count) = rx.recv().await {
        POOL_DESTROYER
            .lock()
            .await
            .as_mut()
            .unwrap()
            .destroying_pools_map
            .increment_destroyed_objects(guid, object_count as u64);

        // Update PoolPhys
        pool_phys
            .destroying_state
            .as_mut()
            .unwrap()
            .destroyed_objects += object_count as u64;
        pool_phys.put(&object_access).await;
    }

    // The super object is destroyed last as it is used to keep track of the progress made.
    object_access.delete_object(PoolPhys::key(guid)).await;

    let mut maybe_pool_destroyer = POOL_DESTROYER.lock().await;
    let pool_destroyer = maybe_pool_destroyer.as_mut().unwrap();
    pool_destroyer
        .destroying_pools_map
        .update_destroy_complete(guid);
    pool_destroyer.write().await.unwrap();
}

fn start_destroy_task(object_access: ObjectAccess, guid: PoolGuid) {
    tokio::spawn(async move {
        destroy_task(object_access, guid).await;
    });
}

/// Mark a pool as destroyed and start destroying it in the background.
pub async fn destroy_pool(object_access: ObjectAccess, guid: PoolGuid, total_data_objects: u64) {
    let mut maybe_pool_destroyer = POOL_DESTROYER.lock().await;
    let pool_destroyer = maybe_pool_destroyer.as_mut().unwrap();
    pool_destroyer
        .mark_pool_destroying(&object_access, guid, total_data_objects)
        .await;
    pool_destroyer
        .add_zpool_destroy_cache(&object_access, guid)
        .await
        .unwrap();
    start_destroy_task(object_access, guid);
}

/// Resume destroying a pool that was previously marked for destroying.
pub async fn resume_destroy(object_access: ObjectAccess, guid: PoolGuid) -> Result<()> {
    // Fail the request if resumption of deletion is being requested on a pool that is not in destroyed state.
    let mut maybe_pool_destroyer = POOL_DESTROYER.lock().await;
    let pool_destroyer = maybe_pool_destroyer.as_mut().unwrap();
    match PoolPhys::get(&object_access, guid).await {
        Ok(pool_phys) => match pool_phys.destroying_state {
            Some(_) => {
                pool_destroyer
                    .add_zpool_destroy_cache(&object_access, guid)
                    .await?;
                start_destroy_task(object_access, guid);
                Ok(())
            }
            None => Err(anyhow!("pool {} not in destroyed state", guid)),
        },
        Err(error) => Err(anyhow!("pool {} not found, {:?}", guid, error)),
    }
}

/// Retrieve the PoolDestroyer's list of pools that are either being destroyed or have been destroyed.
pub async fn get_destroy_list() -> NvList {
    let maybe_pool_destroyer = POOL_DESTROYER.lock().await;
    maybe_pool_destroyer
        .as_ref()
        .unwrap()
        .destroying_pools_map
        .to_nvlist()
}

/// Remove pools that have been successfully destroyed from the PoolDestroyer's list of pools.
pub async fn remove_not_in_progress() {
    let mut maybe_pool_destroyer = POOL_DESTROYER.lock().await;
    let pool_destroyer = maybe_pool_destroyer.as_mut().unwrap();
    pool_destroyer
        .destroying_pools_map
        .remove_not_in_progress()
        .await;
    pool_destroyer.write().await.unwrap();
}

pub async fn init_pool_destroyer(socket_dir: &str) {
    // The PoolDestroyer should be initialized only once.
    let mut maybe_pool_destroyer = POOL_DESTROYER.lock().await;
    assert!(maybe_pool_destroyer.is_none());

    // Filename for zpool-destroy cache file.
    let mut destroyer = PoolDestroyer {
        destroying_pools_map: Default::default(),
        destroy_cache_filename: format!("{}/zpool_destroy.cache", socket_dir),
    };
    destroyer.init().await.unwrap();

    *maybe_pool_destroyer = Some(destroyer);
}

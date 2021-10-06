use crate::object_access::{OAError, ObjectAccess};
use anyhow::Context;
use lazy_static::lazy_static;
use log::{debug, info, trace, warn};
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    sync::{Arc, Weak},
    time::{Duration, Instant, SystemTime},
};
use tokio::sync::watch::{self, Receiver};
use util::get_tunable;
use util::maybe_die_with;
use uuid::Uuid;

lazy_static! {
    pub static ref LEASE_DURATION: Duration =
        Duration::from_millis(get_tunable("lease_duration_ms", 10_000));
    pub static ref HEARTBEAT_INTERVAL: Duration =
        Duration::from_millis(get_tunable("heartbeat_interval_ms", 1_000));
    pub static ref WRITE_TIMEOUT: Duration =
        Duration::from_millis(get_tunable("write_timeout_ms", 2_000));
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct HeartbeatPhys {
    pub timestamp: SystemTime,
    pub hostname: String,
    pub lease_duration: Duration,
    pub id: Uuid,
}

impl HeartbeatPhys {
    fn key(id: Uuid) -> String {
        format!("zfs/agents/{}", id.to_string())
    }

    pub async fn get(object_access: &ObjectAccess, id: Uuid) -> anyhow::Result<Self> {
        let buf = object_access.get_object_impl(Self::key(id), None).await?;
        let this: Self = serde_json::from_slice(&buf)
            .with_context(|| format!("Failed to decode contents of {}", Self::key(id)))?;
        debug!("got {:#?}", this);
        assert_eq!(this.id, id);
        Ok(this)
    }

    pub async fn put_timeout(
        &self,
        object_access: &ObjectAccess,
        timeout: Option<Duration>,
    ) -> Result<rusoto_s3::PutObjectOutput, OAError<rusoto_s3::PutObjectError>> {
        maybe_die_with(|| format!("before putting {:#?}", self));
        debug!("putting {:#?}", self);
        let buf = serde_json::to_vec(&self).unwrap();
        object_access
            .put_object_timed(Self::key(self.id), buf, timeout)
            .await
    }

    pub async fn delete(object_access: &ObjectAccess, id: Uuid) {
        object_access.delete_object(Self::key(id)).await;
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
struct HeartbeatImpl {
    endpoint: String,
    region: String,
    bucket: String,
}
pub struct HeartbeatGuard {
    _key: Arc<()>,
}

lazy_static! {
    static ref HEARTBEAT: Arc<std::sync::Mutex<HashMap<HeartbeatImpl, Weak<()>>>> =
        Default::default();
    static ref HEARTBEAT_INIT: Arc<std::sync::Mutex<HashMap<HeartbeatImpl, Receiver<bool>>>> =
        Default::default();
}

pub async fn start_heartbeat(object_access: ObjectAccess, id: Uuid) -> HeartbeatGuard {
    let key = HeartbeatImpl {
        endpoint: object_access.endpoint(),
        region: object_access.region(),
        bucket: object_access.bucket(),
    };
    let (guard, tx_opt, rx_opt, found) = {
        let mut heartbeats = HEARTBEAT.lock().unwrap();
        match heartbeats.get(&key) {
            None => {
                let value = Arc::new(());
                heartbeats.insert(key.clone(), Arc::downgrade(&value));
                let (tx, rx) = watch::channel(false);
                HEARTBEAT_INIT
                    .lock()
                    .unwrap()
                    .insert(key.clone(), rx.clone());
                (HeartbeatGuard { _key: value }, Some(tx), Some(rx), false)
            }
            Some(val_ref) => {
                debug!("existing entry found");
                match val_ref.upgrade() {
                    None => {
                        /*
                         * In this case, there is already a heartbeat thread that would terminate
                         * on its next iteration. Replace the existing weak ref with a new one, and
                         * let it keep running.
                         */
                        let value = Arc::new(());
                        heartbeats.insert(key.clone(), Arc::downgrade(&value));
                        return HeartbeatGuard { _key: value };
                    }
                    /*
                     * We have to process this outside of the block so that the compiler
                     * realizes the mutex is dropped across the await.
                     */
                    Some(val) => (
                        HeartbeatGuard { _key: val },
                        None,
                        HEARTBEAT_INIT
                            .lock()
                            .unwrap()
                            .get(&key)
                            .map(Receiver::clone),
                        true,
                    ),
                }
            }
        }
    };
    if found {
        /*
         * There is an existing heartbeat with references. If there is an init in
         * progress, we wait for the init to finish before returning.
         */
        debug!("upgrade succeeded, using existing heartbeat");
        if let Some(mut rx) = rx_opt {
            let result = rx.changed().await;
            assert!(result.is_err() || *rx.borrow());
        }
        return guard;
    }
    let mut rx = rx_opt.unwrap();
    let tx = tx_opt.unwrap();
    tokio::spawn(async move {
        let mut last_heartbeat: Option<Instant> = None;
        info!("Starting heartbeat with id {}", id);
        let mut interval = tokio::time::interval(*HEARTBEAT_INTERVAL);
        loop {
            interval.tick().await;
            if let Some(time) = last_heartbeat {
                let since = Instant::now().duration_since(time);
                if since > 2 * *HEARTBEAT_INTERVAL {
                    debug!("Heartbeat interval significantly over: {:?}", since);
                } else if since > *HEARTBEAT_INTERVAL {
                    trace!("Heartbeat interval slightly over: {:?}", since);
                }
            }
            {
                let fut_opt = {
                    let mut heartbeats = HEARTBEAT.lock().unwrap();
                    // We can almost use or_else here, but that doesn't let us change types.
                    match heartbeats.get(&key).unwrap().upgrade() {
                        None => {
                            heartbeats.remove(&key);
                            info!("Stopping heartbeat with id {}", id);
                            Some(HeartbeatPhys::delete(&object_access, id))
                        }
                        Some(_) => None,
                    }
                };
                if let Some(fut) = fut_opt {
                    fut.await;
                    return;
                }
            }
            if let Some(time) = last_heartbeat {
                let since = Instant::now().duration_since(time);
                if since > 2 * *HEARTBEAT_INTERVAL {
                    debug!("Heartbeat locking significantly over: {:?}", since);
                } else if since > *HEARTBEAT_INTERVAL {
                    trace!("Heartbeat locking slightly over: {:?}", since);
                }
            }
            let heartbeat = HeartbeatPhys {
                timestamp: SystemTime::now(),
                hostname: hostname::get().unwrap().into_string().unwrap(),
                lease_duration: *LEASE_DURATION,
                id,
            };
            let instant = Instant::now();
            let result = heartbeat.put_timeout(&object_access, None).await;
            if lease_timed_out(last_heartbeat) {
                panic!("Suspending pools due to lease timeout");
            }
            if result.is_ok() {
                if last_heartbeat.is_none() {
                    tx.send(true).unwrap();
                }
                last_heartbeat = Some(instant);
            }
        }
    });
    let result = rx.changed().await;
    assert!(result.is_err() || *rx.borrow());
    guard
}

fn lease_timed_out(last_heartbeat: Option<Instant>) -> bool {
    match last_heartbeat {
        Some(time) => {
            let since = Instant::now().duration_since(time);
            if since > 2 * *LEASE_DURATION / 3 {
                warn!("Extreme heartbeat delay: {:?}", since);
            } else if since > *LEASE_DURATION / 3 {
                info!("Long heartbeat delay: {:?}", since);
            } else {
                debug!("Short heartbeat delay: {:?}", since);
            }
            since >= *LEASE_DURATION
        }
        None => false,
    }
}

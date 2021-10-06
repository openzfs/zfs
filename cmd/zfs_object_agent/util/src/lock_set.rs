use log::*;
use std::fmt::Debug;
use std::{
    collections::{hash_map, HashMap},
    hash::Hash,
    sync::{Arc, Mutex},
};
use tokio::sync::watch;

#[derive(Default, Debug, Clone)]
pub struct LockSet<V: Hash + Eq + Copy + Debug> {
    locks: Arc<Mutex<HashMap<V, watch::Receiver<()>>>>,
}

pub struct LockedItem<V: Hash + Eq + Copy + Debug> {
    value: V,
    tx: watch::Sender<()>,
    set: LockSet<V>,
}

impl<V: Hash + Eq + Copy + Debug> Drop for LockedItem<V> {
    fn drop(&mut self) {
        trace!("{:?}: removing lock", self.value);
        let rx = self.set.locks.lock().unwrap().remove(&self.value);
        assert!(rx.is_some());
        // This unwrap can't fail because there is still a receiver, `rx`.
        self.tx.send(()).unwrap();
    }
}

impl<V: Hash + Eq + Copy + Debug> LockedItem<V> {
    pub fn value(&self) -> &V {
        &self.value
    }
}

impl<V: Hash + Eq + Copy + Debug> LockSet<V> {
    pub fn new() -> Self {
        Self {
            locks: Default::default(),
        }
    }

    pub async fn lock(&self, value: V) -> LockedItem<V> {
        let tx = loop {
            let mut rx = {
                match self.locks.lock().unwrap().entry(value) {
                    hash_map::Entry::Occupied(oe) => oe.get().clone(),
                    hash_map::Entry::Vacant(ve) => {
                        let (tx, rx) = watch::channel(());
                        ve.insert(rx);
                        break tx;
                    }
                }
            };
            trace!("{:?}: waiting for existing lock", value);
            // Note: since we don't hold the locks mutex now, the corresponding
            // LockedItem may have been dropped, in which case the sender was
            // dropped.  In this case, the changed() Result will be an Err,
            // which we ignore with ok().
            rx.changed().await.ok();
        };
        trace!("{:?}: inserted new lock", value);

        LockedItem {
            value,
            tx,
            set: self.clone(),
        }
    }
}

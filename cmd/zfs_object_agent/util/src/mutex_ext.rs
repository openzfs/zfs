//! This module contains a trait which extends tokio::sync::Mutex.  It adds a
//! new method, .lock_non_send(), which locks the mutex like .lock(), but
//! returns a new kind of guard which can not be sent between threads.  This is
//! useful if you want to ensure that .await is not used while the mutex is
//! locked by some callers, but .await can be used from other callers (that use
//! tokio::sync::Mutex::lock() directly).

use async_trait::async_trait;
use std::marker::PhantomData;
use std::ops::Deref;
use std::ops::DerefMut;

pub struct NonSendMutexGuard<'a, T> {
    inner: tokio::sync::MutexGuard<'a, T>,
    // force this to not be Send
    _marker: PhantomData<*const ()>,
}

impl<'a, T> Deref for NonSendMutexGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<'a, T> DerefMut for NonSendMutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

#[async_trait]
pub trait MutexExt<'a, T> {
    async fn lock_non_send(&'a self) -> NonSendMutexGuard<'a, T>;
}

#[async_trait]
impl<'a, T: Send> MutexExt<'a, T> for tokio::sync::Mutex<T> {
    async fn lock_non_send(&'a self) -> NonSendMutexGuard<'a, T> {
        NonSendMutexGuard {
            inner: self.lock().await,
            _marker: PhantomData,
        }
    }
}

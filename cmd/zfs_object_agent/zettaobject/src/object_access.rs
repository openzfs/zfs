use anyhow::{anyhow, Context, Result};
use async_stream::stream;
use bytes::Bytes;
use core::time::Duration;
use futures::future::Either;
use futures::stream::{self, StreamExt};
use futures::{future, Future, TryStreamExt};
use futures_core::Stream;
use http::StatusCode;
use lazy_static::lazy_static;
use log::*;
use lru::LruCache;
use rand::prelude::*;
use rusoto_core::{ByteStream, RusotoError};
use rusoto_credential::{AutoRefreshingProvider, ChainProvider, ProfileProvider};
use rusoto_s3::{
    Delete, DeleteObjectsRequest, GetObjectRequest, HeadObjectOutput, HeadObjectRequest,
    ListObjectsV2Request, ObjectIdentifier, PutObjectError, PutObjectOutput, PutObjectRequest,
    S3Client, S3,
};
use std::convert::TryFrom;
use std::error::Error;
use std::iter;
use std::sync::Arc;
use std::time::Instant;
use std::{collections::HashMap, fmt::Display};
use tokio::{sync::watch, time::error::Elapsed};
use util::get_tunable;

struct ObjectCache {
    // XXX cache key should include Bucket
    cache: LruCache<String, Arc<Vec<u8>>>,
    reading: HashMap<String, watch::Receiver<Option<Arc<Vec<u8>>>>>,
}

lazy_static! {
    static ref CACHE: std::sync::Mutex<ObjectCache> = std::sync::Mutex::new(ObjectCache {
        cache: LruCache::new(100),
        reading: HashMap::new(),
    });
    static ref NON_RETRYABLE_ERRORS: Vec<StatusCode> = vec![
        StatusCode::BAD_REQUEST,
        StatusCode::FORBIDDEN,
        StatusCode::NOT_FOUND,
        StatusCode::METHOD_NOT_ALLOWED,
        StatusCode::PRECONDITION_FAILED,
        StatusCode::PAYLOAD_TOO_LARGE,
    ];
    // log operations that take longer than this with info!()
    static ref LONG_OPERATION_DURATION: Duration = Duration::from_secs(get_tunable("long_operation_secs", 2));

    pub static ref OBJECT_DELETION_BATCH_SIZE: usize = get_tunable("object_deletion_batch_size", 1000);
}

#[derive(Clone)]
pub struct ObjectAccess {
    client: rusoto_s3::S3Client,
    bucket_str: String,
    readonly: bool,
    region_str: String,
    endpoint_str: String,
    credentials_profile: Option<String>,
}

#[derive(Debug)]
#[allow(clippy::upper_case_acronyms)]
pub enum OAError<E> {
    TimeoutError(Elapsed),
    RequestError(RusotoError<E>),
    Other(anyhow::Error),
}

impl<E> Display for OAError<E>
where
    E: std::error::Error + 'static,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            OAError::TimeoutError(e) => e.fmt(f),
            OAError::RequestError(e) => e.fmt(f),
            OAError::Other(e) => e.fmt(f),
        }
    }
}

impl<E> Error for OAError<E> where E: std::error::Error + 'static {}

impl<E> From<RusotoError<E>> for OAError<E> {
    fn from(e: RusotoError<E>) -> Self {
        Self::RequestError(e)
    }
}

async fn retry_impl<F, O, E>(msg: &str, f: impl Fn() -> F) -> Result<O, OAError<E>>
where
    E: core::fmt::Debug,
    F: Future<Output = Result<O, OAError<E>>>,
{
    let mut delay = Duration::from_secs_f64(thread_rng().gen_range(0.001..0.2));
    loop {
        match f().await {
            res @ Ok(_) => return res,
            res @ Err(OAError::RequestError(RusotoError::Service(_))) => return res,
            res @ Err(OAError::RequestError(RusotoError::Credentials(_))) => return res,
            Err(OAError::RequestError(RusotoError::Unknown(bhr))) => {
                if NON_RETRYABLE_ERRORS.contains(&bhr.status) {
                    return Err(OAError::RequestError(RusotoError::Unknown(bhr)));
                }
            }
            Err(e) => {
                debug!(
                    "{} returned: {:?}; retrying in {}ms",
                    msg,
                    e,
                    delay.as_millis()
                );
                if delay > *LONG_OPERATION_DURATION {
                    info!(
                        "long retry: {} returned: {:?}; retrying in {:?}",
                        msg, e, delay
                    );
                }
            }
        }
        tokio::time::sleep(delay).await;
        delay = delay.mul_f64(thread_rng().gen_range(1.5..2.5));
    }
}

async fn retry<F, O, E>(
    msg: &str,
    timeout_opt: Option<Duration>,
    f: impl Fn() -> F,
) -> Result<O, OAError<E>>
where
    E: core::fmt::Debug,
    F: Future<Output = Result<O, OAError<E>>>,
{
    trace!("{}: begin", msg);
    let begin = Instant::now();
    let result = match timeout_opt {
        Some(timeout) => match tokio::time::timeout(timeout, retry_impl(msg, f)).await {
            Err(e) => Err(OAError::TimeoutError(e)),
            Ok(res2) => res2,
        },
        None => retry_impl(msg, f).await,
    };
    let elapsed = begin.elapsed();
    trace!("{}: returned in {}ms", msg, elapsed.as_millis());
    if elapsed > *LONG_OPERATION_DURATION {
        info!(
            "long completion: {}: returned in {:.1}s",
            msg,
            elapsed.as_secs_f64()
        );
    }
    result
}

impl ObjectAccess {
    fn get_custom_region(endpoint: &str, region_str: &str) -> rusoto_core::Region {
        rusoto_core::Region::Custom {
            name: region_str.to_owned(),
            endpoint: endpoint.to_owned(),
        }
    }

    pub fn get_client_with_creds(
        endpoint: &str,
        region_str: &str,
        access_key_id: &str,
        secret_access_key: &str,
    ) -> S3Client {
        info!("region: {:?}", region_str);
        info!("Endpoint: {}", endpoint);

        let http_client = rusoto_core::HttpClient::new().unwrap();
        let creds = rusoto_core::credential::StaticProvider::new(
            access_key_id.to_string(),
            secret_access_key.to_string(),
            None,
            None,
        );
        let region = ObjectAccess::get_custom_region(endpoint, region_str);
        rusoto_s3::S3Client::new_with(http_client, creds, region)
    }

    pub fn get_client(endpoint: &str, region_str: &str, profile: Option<String>) -> S3Client {
        info!("region: {}", region_str);
        info!("Endpoint: {}", endpoint);
        info!("Profile: {:?}", profile);

        let auto_refreshing_provider =
            AutoRefreshingProvider::new(ChainProvider::with_profile_provider(
                ProfileProvider::with_default_credentials(
                    profile.unwrap_or_else(|| "default".to_owned()),
                )
                .unwrap(),
            ))
            .unwrap();

        let http_client = rusoto_core::HttpClient::new().unwrap();
        let region = ObjectAccess::get_custom_region(endpoint, region_str);
        rusoto_s3::S3Client::new_with(http_client, auto_refreshing_provider, region)
    }

    pub fn from_client(
        client: rusoto_s3::S3Client,
        bucket: &str,
        readonly: bool,
        endpoint: &str,
        region: &str,
    ) -> Self {
        ObjectAccess {
            client,
            bucket_str: bucket.to_string(),
            readonly,
            region_str: region.to_string(),
            endpoint_str: endpoint.to_string(),
            credentials_profile: None,
        }
    }

    pub fn new(
        endpoint: &str,
        region_str: &str,
        bucket: &str,
        profile: Option<String>,
        readonly: bool,
    ) -> Self {
        let credentials_profile = profile.clone();
        let client = ObjectAccess::get_client(endpoint, region_str, profile);

        ObjectAccess {
            client,
            bucket_str: bucket.to_string(),
            readonly,
            region_str: region_str.to_string(),
            endpoint_str: endpoint.to_string(),
            credentials_profile,
        }
    }

    pub fn release_client(self) -> S3Client {
        self.client
    }

    pub async fn get_object_impl(&self, key: String, timeout: Option<Duration>) -> Result<Vec<u8>> {
        let msg = format!("get {}", key);
        let v = retry(&msg, timeout, || async {
            let req = GetObjectRequest {
                bucket: self.bucket_str.clone(),
                key: key.clone(),
                ..Default::default()
            };
            let output = self.client.get_object(req).await?;
            let begin = Instant::now();
            let mut v =
                Vec::with_capacity(usize::try_from(output.content_length.unwrap_or(0)).unwrap());
            let mut count = 0;
            match output
                .body
                .unwrap()
                .try_for_each(|b| {
                    v.extend_from_slice(&b);
                    count += 1;
                    future::ready(Ok(()))
                })
                .await
            {
                Err(e) => {
                    debug!("{}: error while reading ByteStream: {}", msg, e);
                    Err(OAError::RequestError(e.into()))
                }
                Ok(_) => {
                    trace!(
                        "{}: got {} bytes of data in {} chunks in {}ms",
                        msg,
                        v.len(),
                        count,
                        begin.elapsed().as_millis()
                    );
                    Ok(v)
                }
            }
        })
        .await
        .with_context(|| format!("Failed to {}", msg))?;

        Ok(v)
    }

    pub async fn get_object_uncached(&self, key: String) -> Result<Arc<Vec<u8>>> {
        let vec = self.get_object_impl(key.clone(), None).await?;
        // Note: we *should* have the same data from S3 (in the `vec`) and in
        // the cache, so this invalidation is normally not necessary.  However,
        // in case a bug (or undetected RAM error) resulted in incorrect cached
        // data, we want to invalidate the cache so that we won't get the bad
        // cached data again.
        Self::invalidate_cache(key, &vec);
        Ok(Arc::new(vec))
    }

    pub async fn get_object(&self, key: String) -> Result<Arc<Vec<u8>>> {
        let either = {
            // need this block separate so that we can drop the mutex before the .await
            let mut c = CACHE.lock().unwrap();
            match c.cache.get(&key) {
                Some(v) => {
                    trace!("found {} in cache", key);
                    return Ok(v.clone());
                }
                None => match c.reading.get(&key) {
                    None => {
                        let (tx, rx) = watch::channel::<Option<Arc<Vec<u8>>>>(None);
                        c.reading.insert(key.clone(), rx);
                        Either::Left(async move {
                            let v = Arc::new(self.get_object_impl(key.clone(), None).await?);
                            let mut myc = CACHE.lock().unwrap();
                            tx.send(Some(v.clone())).unwrap();
                            myc.cache.put(key.to_string(), v.clone());
                            myc.reading.remove(&key);
                            Ok(v)
                        })
                    }
                    Some(rx) => {
                        debug!("found {} read in progress", key);
                        let mut myrx = rx.clone();
                        Either::Right(async move {
                            if let Some(vec) = myrx.borrow().as_ref() {
                                return Ok(vec.clone());
                            }
                            // Note: "else" or "match" statement not allowed
                            // here because the .borrow()'ed Ref is not dropped
                            // until the end of the else/match

                            // XXX if the sender drops due to
                            // get_object_impl() failing, we don't get a
                            // very good error message, but maybe that
                            // doesn't matter since the behavior is
                            // otherwise correct (we return an Error)
                            // XXX should we make a wrapper around the
                            // watch::channel that has borrow() wait until the
                            // first value is sent?
                            myrx.changed().await?;
                            let b = myrx.borrow();
                            // Note: we assume that the once it's changed, it
                            // has to be Some()
                            Ok(b.as_ref().unwrap().clone())
                        })
                    }
                },
            }
        };
        either.await
    }

    fn list_impl(
        &self,
        prefix: String,
        start_after: Option<String>,
        use_delimiter: bool,
        list_prefixes: bool,
    ) -> impl Stream<Item = String> {
        let mut continuation_token = None;
        // XXX ObjectAccess should really be refcounted (behind Arc)
        let client = self.client.clone();
        let bucket = self.bucket_str.clone();
        let delimiter = match use_delimiter {
            true => Some("/".to_string()),
            false => None,
        };
        stream! {
            loop {
                let output = retry(
                    &format!("list {} (after {:?})", prefix, start_after),
                    None,
                    || async {
                        let req = ListObjectsV2Request {
                            bucket: bucket.clone(),
                            continuation_token: continuation_token.clone(),
                            delimiter: delimiter.clone(),
                            fetch_owner: Some(false),
                            prefix: Some(prefix.clone()),
                            start_after: start_after.clone(),
                            ..Default::default()
                        };
                        // Note: Ok(...?) converts the RusotoError to an OAError for us
                        Ok(client.list_objects_v2(req).await?)
                    },
                )
                .await
                .unwrap();

                if list_prefixes {
                    if let Some(prefixes) = output.common_prefixes {
                        for prefix in prefixes {
                            yield prefix.prefix.unwrap();
                        }
                    }
                } else {
                    if let Some(objects) = output.contents {
                        for object in objects {
                            yield object.key.unwrap();
                        }
                    }
                }
                if output.next_continuation_token.is_none() {
                    break;
                }
                continuation_token = output.next_continuation_token;
            }
        }
    }

    pub fn list_objects(
        &self,
        prefix: String,
        start_after: Option<String>,
        use_delimiter: bool,
    ) -> impl Stream<Item = String> {
        self.list_impl(prefix, start_after, use_delimiter, false)
    }

    pub fn list_prefixes(&self, prefix: String) -> impl Stream<Item = String> {
        self.list_impl(prefix, None, true, true)
    }

    pub async fn collect_objects(
        &self,
        prefix: String,
        start_after: Option<String>,
    ) -> Vec<String> {
        self.list_objects(prefix, start_after, true).collect().await
    }

    pub async fn head_object(&self, key: String) -> Option<HeadObjectOutput> {
        let res = retry(&format!("head {}", key), None, || async {
            let req = HeadObjectRequest {
                bucket: self.bucket_str.clone(),
                key: key.clone(),
                ..Default::default()
            };
            // Note: Ok(...?) converts the RusotoError to an OAError for us
            Ok(self.client.head_object(req).await?)
        })
        .await;
        res.ok()
    }

    pub async fn object_exists(&self, key: String) -> bool {
        self.head_object(key).await.is_some()
    }

    async fn put_object_impl(
        &self,
        key: String,
        data: Vec<u8>,
        timeout: Option<Duration>,
    ) -> Result<PutObjectOutput, OAError<PutObjectError>> {
        let len = data.len();
        let bytes = Bytes::from(data);
        assert!(!self.readonly);
        retry(&format!("put {} ({} bytes)", key, len), timeout, || async {
            let my_bytes = bytes.clone();
            let stream = ByteStream::new_with_size(stream! { yield Ok(my_bytes)}, len);

            let req = PutObjectRequest {
                bucket: self.bucket_str.clone(),
                key: key.clone(),
                body: Some(stream),
                ..Default::default()
            };
            // Note: Ok(...?) converts the RusotoError to an OAError for us
            Ok(self.client.put_object(req).await?)
        })
        .await
    }

    fn invalidate_cache(key: String, data: &[u8]) {
        let mut c = CACHE.lock().unwrap();
        if c.cache.contains(&key) {
            trace!("found {} in cache - invalidating", key);
            // XXX unfortuate to be copying; this happens every time when
            // freeing (we get/modify/put the object).  Maybe when freeing,
            // the get() should not add to the cache since it's probably
            // just polluting.
            c.cache.put(key, Arc::new(data.to_vec()));
        }
    }

    pub async fn put_object(&self, key: String, data: Vec<u8>) {
        Self::invalidate_cache(key.clone(), &data);

        self.put_object_impl(key, data, None).await.unwrap();
    }

    pub async fn put_object_timed(
        &self,
        key: String,
        data: Vec<u8>,
        timeout: Option<Duration>,
    ) -> Result<PutObjectOutput, OAError<PutObjectError>> {
        Self::invalidate_cache(key.clone(), &data);

        self.put_object_impl(key, data, timeout).await
    }

    pub async fn delete_object(&self, key: String) {
        self.delete_objects(stream::iter(iter::once(key))).await;
    }

    // Note: Stream is of raw keys (with prefix)
    pub async fn delete_objects<S: Stream<Item = String>>(&self, stream: S) {
        // Note: we intentionally issue the delete calls serially because it
        // doesn't seem to improve performance if we issue them in parallel
        // (using StreamExt::for_each_concurrent()).
        stream
            .chunks(*OBJECT_DELETION_BATCH_SIZE)
            .for_each(|chunk| async move {
                let msg = format!("delete {} objects including {}", chunk.len(), &chunk[0]);
                assert!(!self.readonly);
                retry(&msg, None, || async {
                    let req = DeleteObjectsRequest {
                        bucket: self.bucket_str.clone(),
                        delete: Delete {
                            objects: chunk
                                .iter()
                                .map(|key| ObjectIdentifier {
                                    key: key.clone(),
                                    ..Default::default()
                                })
                                .collect(),
                            quiet: Some(true),
                        },
                        ..Default::default()
                    };
                    let output = self.client.delete_objects(req).await?;
                    match output.errors {
                        Some(errs) => match errs.get(0) {
                            Some(e) => Err(OAError::Other(anyhow!("{:?}", e))),
                            None => Ok(()),
                        },
                        None => Ok(()),
                    }
                })
                .await
                .unwrap();
            })
            .await;
    }

    pub fn bucket(&self) -> String {
        self.bucket_str.clone()
    }

    pub fn region(&self) -> String {
        self.region_str.clone()
    }

    pub fn endpoint(&self) -> String {
        self.endpoint_str.clone()
    }

    pub fn credentials_profile(&self) -> Option<String> {
        self.credentials_profile.clone()
    }

    pub fn readonly(&self) -> bool {
        self.readonly
    }
}

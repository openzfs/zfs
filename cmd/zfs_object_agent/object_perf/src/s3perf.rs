use futures::stream;
use futures::{future, StreamExt};
use log::*;
use metered::common::*;
use metered::hdr_histogram::AtomicHdrHistogram;
use metered::metered;
use metered::time_source::StdInstantMicros;
use std::cmp::max;
use std::convert::TryInto;
use std::error::Error;
use std::string::String;
use std::sync::Arc;
use std::time::{Duration, Instant};
use zettaobject::ObjectAccess;

enum WriteTestBounds {
    Time(Duration),
    Objects(u64),
}

#[derive(Default, Clone)]
struct Perf {
    metrics: Arc<PerfMetrics>,
}

#[metered(registry=PerfMetrics)]
impl Perf {
    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    async fn put(&self, object_access: &ObjectAccess, key: String, data: Vec<u8>) {
        object_access.put_object(key, data).await;
    }

    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    async fn get(&self, object_access: &ObjectAccess, key: String) {
        object_access.get_object(key).await.unwrap();
    }

    fn log_metrics(&self, duration: Duration) {
        let my_perf = self.clone();
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(duration);
            loop {
                interval.tick().await;
                info!("{:#?}", my_perf.metrics);
            }
        });
    }

    async fn read_objects(
        &self,
        object_access: &ObjectAccess,
        key_prefix: String,
        qdepth: u64,
        duration: Duration,
    ) {
        let num_objects = object_access
            .list_objects(key_prefix.clone(), None, true)
            .fold(0, |count, _key| async move { count + 1 })
            .await;
        let mut key_id = 0;
        let start = Instant::now();
        stream::repeat_with(|| {
            let my_perf = self.clone();
            let my_object_access = object_access.clone();
            let my_key_prefix = key_prefix.clone();
            key_id += 1;
            tokio::spawn(async move {
                my_perf
                    .get(
                        &my_object_access,
                        format!("{}{}", my_key_prefix, key_id % num_objects + 1),
                    )
                    .await;
            })
        })
        .take_while(|_| future::ready(start.elapsed() < duration))
        .buffer_unordered(qdepth.try_into().unwrap())
        .for_each(|_| future::ready(()))
        .await;
    }

    async fn write_objects(
        &self,
        object_access: &ObjectAccess,
        key_prefix: String,
        objsize: u64,
        qdepth: u64,
        bounds: WriteTestBounds,
    ) {
        let data = vec![0; objsize.try_into().unwrap()];
        let mut key_id: u64 = 0;
        let start = Instant::now();
        let put_lambda = || {
            let my_data = data.clone();
            let my_perf = self.clone();
            let my_object_access = object_access.clone();
            let my_key_prefix = key_prefix.clone();
            key_id += 1;
            tokio::spawn(async move {
                my_perf
                    .put(
                        &my_object_access,
                        format!("{}{}", my_key_prefix, key_id),
                        my_data,
                    )
                    .await
            })
        };
        let put_stream = stream::repeat_with(put_lambda);

        match bounds {
            WriteTestBounds::Time(duration) => {
                put_stream
                    .take_while(|_| future::ready(start.elapsed() < duration))
                    .buffer_unordered(qdepth.try_into().unwrap())
                    .for_each(|_| future::ready(()))
                    .await;
            }
            WriteTestBounds::Objects(num_objects) => {
                put_stream
                    .take(num_objects.try_into().unwrap())
                    .buffer_unordered(qdepth.try_into().unwrap())
                    .for_each(|_| future::ready(()))
                    .await;
            }
        }
    }
}

pub async fn write_test(
    object_access: &ObjectAccess,
    key_prefix: String,
    objsize: u64,
    qdepth: u64,
    duration: Duration,
) -> Result<(), Box<dyn Error>> {
    let perf = Perf::default();
    let bounds = WriteTestBounds::Time(duration);
    perf.log_metrics(Duration::from_secs(1));

    perf.write_objects(object_access, key_prefix.clone(), objsize, qdepth, bounds)
        .await;

    println!("{:#?}", perf.metrics.put);

    object_access
        .delete_objects(object_access.list_objects(key_prefix, None, false))
        .await;

    Ok(())
}

pub async fn read_test(
    object_access: &ObjectAccess,
    key_prefix: String,
    objsize: u64,
    qdepth: u64,
    duration: Duration,
) -> Result<(), Box<dyn Error>> {
    let perf = Perf::default();
    let bounds = WriteTestBounds::Objects(max(qdepth * 10, 200));
    perf.log_metrics(Duration::from_secs(1));

    perf.write_objects(object_access, key_prefix.clone(), objsize, qdepth, bounds)
        .await;

    perf.read_objects(object_access, key_prefix.clone(), qdepth, duration)
        .await;

    println!("{:#?}", perf.metrics.get);

    object_access
        .delete_objects(object_access.list_objects(key_prefix, None, false))
        .await;

    Ok(())
}

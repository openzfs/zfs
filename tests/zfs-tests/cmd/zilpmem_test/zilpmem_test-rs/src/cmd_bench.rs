use anyhow::Context;
use bindings::boolean_t;
use core::ffi::c_void;
use core::time::Duration;
use serde::{Serialize, Serializer};
use std::convert::TryFrom;
use std::convert::TryInto;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::Ordering::SeqCst;
use std::{
    collections::{BTreeMap, HashMap, VecDeque},
    fs::{self},
    sync::{mpsc, Arc},
    thread,
    time::Instant,
};
use structopt::StructOpt;
use zilpmem::BaseChunklenNumchunks;
use zilpmem_test::args_mmap::MmapArgs;
use zilpmem_test::libzpool::zilpmem;
use zilpmem_test::prb_writestats::prb_write_stats;
use zilpmem_test::utils::SendPtr;

#[derive(StructOpt, serde::Serialize)]
pub struct Args {
    #[structopt(long = "cpus-unpinned", conflicts_with = "cpus-pinned")]
    cpus_unpinned: Option<usize>,
    #[structopt(long = "cpus-pinned", conflicts_with = "cpus-unpinned")]
    cpus_pinned: Option<usize>,
    /// each entry is a core id. for each entry, we instantiate a writer thread and pin it to that core
    #[structopt(
        long = "cpus-pinned-list",
        conflicts_with = "cpus-unpinned",
        use_delimiter = true
    )]
    cpus_pinned_list: Vec<usize>,
    #[structopt(long = "committers")]
    committers: Option<usize>,
    #[structopt(long = "runtime")]
    runtime: Option<u64>,
    #[structopt(long = "warmup")]
    warmup: Option<u64>,
    #[structopt(long = "alloc-size")]
    alloc_size: usize,
    #[structopt(long = "chunklen")]
    chunklen: usize,
    #[structopt(flatten)]
    mmap: MmapArgs,
}

fn online_cpu_lists_for_sched_setaffinity() -> anyhow::Result<Vec<usize>> {
    let fpath = "/sys/devices/system/cpu/online";
    // e.g. 0-7,16-23
    let sysfs_str = fs::read_to_string(fpath).context("read")?;

    let lines = sysfs_str.lines().collect::<Vec<_>>();
    if lines.len() != 1 {
        return Err(anyhow::anyhow!(
            "unexpected number of lines ({}) in file {}",
            lines.len(),
            fpath
        ));
    }
    let seqs = lines[0].split(",");
    let mut ret = Vec::new();
    for seq in seqs {
        let comps = seq.split("-").collect::<Vec<_>>();
        if comps.len() == 1 {
            ret.push(comps[0].parse().context("parse int")?);
        } else if comps.len() == 2 {
            let lower = comps[0].parse().context("parse lower")?;
            let upper = comps[1].parse().context("parse upper")?;
            (lower..=upper).for_each(|i| ret.push(i));
        } else {
            return Err(anyhow::anyhow!(
                "unexpected number of components separated by hypen: {:?}",
                comps
            ));
        }
    }

    Ok(ret)
}

pub fn run(args: Args) -> anyhow::Result<()> {
    let start_counting = Arc::new(AtomicBool::new(false));

    let (ncpus, affinity_by_cpu_idx) = if let Some(cpus_unpinned) = args.cpus_unpinned {
        (cpus_unpinned, Arc::new(None))
    } else if let Some(cpus_pinned) = args.cpus_pinned {
        let affinity_list = online_cpu_lists_for_sched_setaffinity()
            .context("get online cpus")?
            .into_iter()
            .take(cpus_pinned)
            .collect::<Vec<_>>();
        (cpus_pinned, Arc::new(Some(affinity_list)))
    } else {
        (
            args.cpus_pinned_list.len(),
            Arc::new(Some(args.cpus_pinned_list.clone())),
        )
    };
    assert!(ncpus > 0, "must sepcify cpus_pinned or cpus_unpinned");
    assert!((&*affinity_by_cpu_idx)
        .as_ref()
        .map(|v: &Vec<_>| v.len() == ncpus)
        .unwrap_or(true));

    let ncommitters = args.committers.unwrap_or(ncpus);

    let prb = SendPtr::from(unsafe { zilpmem::prb_alloc(ncommitters.try_into().unwrap()) });

    let mut mapping = args.mmap.mmap().expect("mmap");
    let bcn = BaseChunklenNumchunks::from_into_base_len(&mut mapping, args.chunklen);
    bcn.add_to_prb_for_write(prb.0);

    let stop_hint = Arc::new(AtomicBool::new(false));

    let (writer_done_with_txg_send, writer_done_with_txg_recv) = mpsc::sync_channel(ncpus);

    let mut writers = vec![];
    for cpu in 0..ncpus {
        let affinity = if let Some(affinity_by_cpu_idx) = &*affinity_by_cpu_idx {
            let mut set = nix::sched::CpuSet::new();
            set.set(affinity_by_cpu_idx[cpu]).unwrap();
            Some(set)
        } else {
            None
        };
        let wargs = WriterThread {
            objset_id: (cpu + 1).try_into().unwrap(), /* +1 because objset_id 0 is not allowed */
            affinity,
            prb,
            writer_done_with_txg: writer_done_with_txg_send.clone(),
            start_counting: Arc::clone(&start_counting),
            alloc_size: args.alloc_size,
            stop_hint: Arc::clone(&stop_hint),
        };
        let jh = thread::spawn(|| writer_thread(wargs));
        writers.push(jh);
    }

    let free_thread = thread::spawn({
        let mut active_cpus = ncpus;
        let stop_hint = Arc::clone(&stop_hint);
        move || {
            let mut current_txg = 1;
            let mut waiting_writers = VecDeque::new();
            loop {
                let msg = writer_done_with_txg_recv.recv().unwrap();
                match msg {
                    WriterMsg::Starting(new_txg) => {
                        new_txg.send(Some(current_txg)).unwrap();
                    }
                    WriterMsg::Stopping => {
                        active_cpus -= 1;
                    }
                    WriterMsg::NoSpace { cpu_txg, new_txg } => {
                        assert_eq!(cpu_txg, current_txg);
                        waiting_writers.push_back(new_txg);
                        if waiting_writers.len() == active_cpus {
                            // only when all threads have left a txg are we allowed to call prb_mark_txg_done
                            unsafe {
                                zilpmem::prb_gc(prb.0, current_txg);
                            }

                            let msg = if stop_hint.load(SeqCst) {
                                None
                            } else {
                                current_txg += 1;
                                Some(current_txg)
                            };

                            for waiting in waiting_writers.drain(..) {
                                waiting.send(msg).unwrap();
                            }
                        }
                    }
                }
                if active_cpus == 0 {
                    break;
                }
            }
        }
    });

    ctrlc::set_handler({
        let stop = Arc::clone(&stop_hint);
        move || {
            stop.store(true, SeqCst);
        }
    })
    .unwrap();

    if let Some(warmup_seconds) = args.warmup {
        std::thread::sleep(std::time::Duration::from_secs(warmup_seconds));
    }
    start_counting.store(true, SeqCst);
    if let Some(runtime_seconds) = args.runtime {
        std::thread::spawn({
            let stop_hint = Arc::clone(&stop_hint);
            move || {
                std::thread::sleep(Duration::from_secs(runtime_seconds));
                stop_hint.store(true, SeqCst);
            }
        });
    }

    let mut duration_get_buffer: Vec<HashMap<libc::size_t, Duration>> = Vec::new();
    let mut get_buffer_count: Vec<HashMap<libc::size_t, u64>> = Vec::new();
    let mut stats_sum_by_need_chunks: Vec<HashMap<libc::size_t, prb_write_stats<u64>>> = Vec::new();
    for t in writers {
        let WriterResult {
            allocs_by_need_chunks: t_get_buffer_count,
            sum_time_get_buffer_by_need_chunks: t_get_buffer_duration,
            stats_sum_by_need_chunks: t_stats_sum_by_need_chunks,
        } = t.join().unwrap();
        duration_get_buffer.push(t_get_buffer_duration);
        get_buffer_count.push(t_get_buffer_count);
        stats_sum_by_need_chunks.push(t_stats_sum_by_need_chunks);
    }

    free_thread.join().unwrap();

    fn ordered_map<S, K, V>(value: &HashMap<K, V>, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
        K: std::cmp::Ord + std::hash::Hash + std::cmp::Eq + Serialize,
        V: Serialize,
    {
        let ordered: BTreeMap<_, _> = value.iter().collect();
        ordered.serialize(serializer)
    }

    #[derive(serde::Serialize)]
    struct BenchRes {
        ncpus: usize,
        ncommitters: usize,
        args: Args,
        #[serde(serialize_with = "ordered_map")]
        get_buffer_count_sum_by_need_chunks: HashMap<libc::size_t, u64>,
        #[serde(serialize_with = "ordered_map")]
        get_buffer_duration_sum_by_need_chunks: HashMap<libc::size_t, Duration>,
        #[serde(serialize_with = "ordered_map")]
        get_buffer_duration_avg_by_get_chunks_ns: HashMap<libc::size_t, f64>,
        #[serde(serialize_with = "ordered_map")]
        stats_nanos_avg_by_need_chunks: HashMap<libc::size_t, prb_write_stats<f64>>,
        get_buffer_duration_avg_ns: f64,
        stats_nanos_avg: prb_write_stats<f64>,
    }

    fn fold_values<K, V, F>(a: HashMap<K, V>, b: &HashMap<K, V>, f: F) -> HashMap<K, V>
    where
        K: Copy + std::hash::Hash + std::cmp::Eq,
        V: Copy + Default,
        F: Fn(&V, &V) -> V,
    {
        a.iter()
            .chain(b.iter())
            .fold(HashMap::new(), |mut a, (k, v)| {
                let e: &mut V = a.entry(*k).or_default();
                *e = f(&e, v);
                a
            })
    }

    fn merge_left_values<K, V1, V2, F, V3>(
        a: &HashMap<K, V1>,
        b: &HashMap<K, V2>,
        f: F,
    ) -> HashMap<K, V3>
    where
        K: Copy + std::hash::Hash + std::cmp::Eq,
        V2: Copy + Default,
        F: Fn(&V1, &V2) -> V3,
    {
        let mut res = HashMap::new();
        for (k, v1) in a {
            let v2: V2 = b.get(k).map(|v| *v).unwrap_or_default();
            res.insert(*k, f(&v1, &v2));
        }
        res
    }

    let get_buffer_count_sum_by_need_chunks = get_buffer_count
        .iter()
        .fold(Default::default(), |a, b| fold_values(a, b, |x, y| x + y));
    let get_buffer_duration_sum_by_need_chunks =
        duration_get_buffer.iter().fold(Default::default(), |a, b| {
            fold_values(a, b, |x, y| (*x) + (*y))
        });

    let get_buffer_duration_avg_by_get_chunks_ns = merge_left_values(
        &get_buffer_duration_sum_by_need_chunks,
        &get_buffer_count_sum_by_need_chunks,
        |d, c| (d.as_secs_f64() / (*c as f64)) * 1e9,
    );

    let allocs: u64 = get_buffer_count_sum_by_need_chunks.values().sum();
    let by_need_chunks_weight: HashMap<_, f64> = get_buffer_count_sum_by_need_chunks
        .iter()
        .map(|(i, v)| (*i, (*v as f64) / (allocs as f64)))
        .collect();

    let get_buffer_duration_avg_ns = {
        let duration: Duration = get_buffer_duration_sum_by_need_chunks.values().sum();
        let duration = duration.as_secs_f64(); // FIXME no fractional
        (duration / (allocs as f64)) * 1e9
    };

    let stats_sum_sum_by_need_chunks = stats_sum_by_need_chunks
        .iter()
        .fold(Default::default(), |a, b| fold_values(a, b, |x, y| *x + *y));

    let status_sum_sum_f64_by_need_chunks = stats_sum_sum_by_need_chunks
        .iter()
        .map(|(i, v)| (*i, v.as_f64()))
        .collect();

    // let stats_total_nanos_by_need_chunks = stats_sum_sum_by_need_chunks.iter().map(|(i, v)| (*i, v.nanos_total())).collect::<HashMap<_, _>>();
    // let stats_nanos_percentage_by_need_chunks = merge_left_values(
    //     &status_sum_sum_f64_by_need_chunks,
    //     &stats_total_nanos_by_need_chunks,
    //     |d, c| (d.div_nanos_by(*c as f64)),
    // );

    let stats_nanos_avg_by_need_chunks = merge_left_values(
        &status_sum_sum_f64_by_need_chunks,
        &get_buffer_count_sum_by_need_chunks,
        |d, c| (d.div_nanos_by(*c as f64)),
    );

    let stats_nanos_avg = merge_left_values(
        &stats_nanos_avg_by_need_chunks,
        &by_need_chunks_weight,
        |stats, weight| stats.mul_nanos_by(*weight),
    )
    .values()
    .cloned()
    .sum();

    let res = BenchRes {
        ncpus,
        ncommitters,
        args,
        get_buffer_count_sum_by_need_chunks,
        get_buffer_duration_sum_by_need_chunks,
        get_buffer_duration_avg_by_get_chunks_ns,
        get_buffer_duration_avg_ns,
        stats_nanos_avg_by_need_chunks,
        stats_nanos_avg,
    };

    println!("{}", serde_json::to_string_pretty(&res).unwrap());

    unsafe {
        zilpmem::prb_free(prb.0, true as boolean_t);
    }

    Ok(())
}

struct WriterThread {
    objset_id: u64,
    affinity: Option<nix::sched::CpuSet>,
    prb: SendPtr<zilpmem::prb_t>,
    start_counting: Arc<AtomicBool>,
    writer_done_with_txg: mpsc::SyncSender<WriterMsg>,
    alloc_size: usize,
    stop_hint: Arc<AtomicBool>, // real stop happens through channels
}

enum WriterMsg {
    Starting(mpsc::Sender<Option<u64>>),
    NoSpace {
        cpu_txg: u64,
        new_txg: mpsc::Sender<Option<u64>>,
    },
    Stopping,
}

#[derive(Default)]
struct WriterResult {
    allocs_by_need_chunks: HashMap<libc::size_t, u64>,
    sum_time_get_buffer_by_need_chunks: HashMap<libc::size_t, Duration>,
    stats_sum_by_need_chunks: HashMap<libc::size_t, prb_write_stats<u64>>,
}

fn writer_thread(w: WriterThread) -> WriterResult {
    let mut local_start_counting = false;

    let WriterThread {
        objset_id,
        affinity,
        prb,
        start_counting,
        writer_done_with_txg,
        alloc_size,
        stop_hint,
    } = w;

    if let Some(set) = affinity {
        // println!("pinning cpu {} to set {:?}", cpu, set);
        nix::sched::sched_setaffinity(nix::unistd::Pid::from_raw(0), &set).expect("set affinity");
    }

    let (initial_txg, initial_txg_recv) = mpsc::channel();
    writer_done_with_txg
        .send(WriterMsg::Starting(initial_txg))
        .unwrap();
    let mut cpu_txg = match initial_txg_recv.recv().unwrap() {
        Some(t) => t,
        None => {
            return WriterResult::default();
        }
    };

    let data: Vec<u8> = vec![23 as u8; alloc_size];
    let mut allocs = vec![0, 0];
    let mut durations = vec![Duration::default(), Duration::default()];
    let mut stats_sum = vec![prb_write_stats::default(); 2];

    let hdl = unsafe { bindings::zilpmem_prb_setup_objset(prb.0, objset_id) };
    assert_ne!(hdl, std::ptr::null_mut());

    let mut hdr = std::mem::MaybeUninit::uninit();

    unsafe { bindings::zilpmem_prb_destroy_log(hdl, hdr.as_mut_ptr()) };

    unsafe { bindings::zilpmem_prb_create_log_if_not_exists(hdl, hdr.as_mut_ptr()) };

    loop {
        // attempt an allocation
        loop {
            let start = Instant::now();
            let (slept, stats): (bool, prb_write_stats<_>) = unsafe {
                let mut stats = std::mem::MaybeUninit::uninit();
                let res = bindings::zilpmem_prb_write_entry_with_stats(
                    hdl,
                    cpu_txg,
                    false as u32, /* FIXME parametrize */
                    data.len().try_into().unwrap(),
                    data.as_ptr() as *const c_void,
                    false as u32,
                    stats.as_mut_ptr(),
                );
                let stats = stats.assume_init();
                use zilpmem::WriteStatsExt;
                if res != bindings::prb_write_result_t_PRB_WRITE_OK {
                    assert_eq!(
                        res,
                        bindings::prb_write_result_t_PRB_WRITE_EWOULDSLEEP,
                        "not expecting any failures"
                    );
                }
                (stats.slept(), stats.into())
            };
            let get_buffer_duration = Instant::now() - start;

            if slept {
                break;
            }

            if !local_start_counting {
                local_start_counting = start_counting.load(SeqCst);
            }
            if local_start_counting {
                let get_chunk_calls = usize::try_from(stats.get_chunk_calls).unwrap();
                allocs[get_chunk_calls] += 1;
                durations[get_chunk_calls] += get_buffer_duration;
                stats_sum[get_chunk_calls] += stats;
            }

            // rate-limited check to see if we should stop now instead of when the buffer is full
            if allocs[0] & ((1 << 10) - 1) == 0 && stop_hint.load(SeqCst) {
                break;
            }
        }

        // if allocation fails, wait for free thread to clean up and give us the new txg
        let (new_txg, new_txg_recv) = mpsc::channel();
        writer_done_with_txg
            .send(WriterMsg::NoSpace { cpu_txg, new_txg })
            .unwrap();
        match new_txg_recv.recv().unwrap() {
            Some(t) => {
                cpu_txg = t;
                continue;
            }
            None => {
                writer_done_with_txg.send(WriterMsg::Stopping).unwrap();
                let mut hdr = std::mem::MaybeUninit::uninit();
                unsafe {
                    bindings::zilpmem_prb_teardown_objset(hdl, true as u32, hdr.as_mut_ptr())
                };
                return WriterResult {
                    sum_time_get_buffer_by_need_chunks: durations.into_iter().enumerate().collect(),
                    allocs_by_need_chunks: allocs.into_iter().enumerate().collect(),
                    stats_sum_by_need_chunks: stats_sum.into_iter().enumerate().collect(),
                };
            }
        }
    }
}

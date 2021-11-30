use super::util::*;
use std::convert::TryInto;
use std::{
    collections::{BTreeMap, HashSet},
    ffi::c_void,
    num::NonZeroU64,
};
use bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES;
use zilpmem::ReplayResumeCallbackArgCopied;

#[test]
fn zilpmem_check_replayable() {
    libzpool::init_once();

    use std::iter::FromIterator;
    use zilpmem::replay_node_t;

    #[derive(Debug)]
    struct Test {
        title: &'static str,
        claim_txg: u64,
        hide_entries: &'static [&'static str],
        expect: bindings::check_replayable_result_kind_t,
    }

    #[derive(Debug)]
    struct TestSet {
        title: &'static str,
        entries: BTreeMap<&'static str, SynthEntry>,
        tests: &'static [Test],
    }

    let entrysets = &[
        TestSet {
            title: "straightline",
            entries: maplit::btreemap!{
                "A" => SynthEntry(1,1,1, (0, [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(2,2,1, (1, [(1, 1),(0,0), (0,0)])),
                "C" => SynthEntry(3,3,1, (2, [(1,1),(2,1), (0,0)])),
                "D" => SynthEntry(4,4,1, (3, [(2,1),(3,1),(0,0)])),
            },
            tests: &[
                Test {
                    title: "happypath",
                    claim_txg: 1,
                    hide_entries: &[],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OK,
                },
                Test {
                    title: "tail truncation gen",
                    claim_txg: 1,
                    hide_entries: &["D"],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OK,
                },
                Test {
                    title: "omit middle gen",
                    claim_txg: 1,
                    hide_entries: &["C"],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES,
                },
            ],
        },
        TestSet {
            title: "straightline_3_entries_per_gen",
            entries: maplit::btreemap!{
                "A1" => SynthEntry(1,1,1, (0, [(0,0),(0,0),(0,0)])),
                "A2" => SynthEntry(1,1,2, (0, [(0,0),(0,0),(0,0)])),
                "A3" => SynthEntry(1,1,3, (0, [(0,0),(0,0),(0,0)])),
                "B1" => SynthEntry(2,2,1, (1, [(1,3),(0,0),(0,0)])),
                "B2" => SynthEntry(2,2,2, (1, [(1,3),(0,0),(0,0)])),
                "B3" => SynthEntry(2,2,3, (1, [(1,3),(0,0),(0,0)])),
                "C1" => SynthEntry(3,3,1, (2, [(1,3),(2,3),(0,0)])),
                "C2" => SynthEntry(3,3,2, (2, [(1,3),(2,3),(0,0)])),
                "C3" => SynthEntry(3,3,3, (2, [(1,3),(2,3),(0,0)])),
                "D1" => SynthEntry(4,4,1, (3, [(2,3),(3,3),(0,0)])),
                "D2" => SynthEntry(4,4,2, (3, [(2,3),(3,3),(0,0)])),
                "D3" => SynthEntry(4,4,3, (3, [(2,3),(3,3),(0,0)])),
            },
            tests: &[
                Test {
                    title: "happypath 3_entries_per_gen",
                    claim_txg: 1,
                    hide_entries: &[],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OK
                },
                Test {
                    title: "omit entry: firstgen",
                    claim_txg: 1,
                    hide_entries: &["A2"],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES,
                },
                Test {
                    title: "omit entry: middle gen",
                    claim_txg: 1,
                    hide_entries: &["C3"],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES,
                },
                Test {
                    title: "omit entry: lastgen",
                    claim_txg: 1,
                    hide_entries: &["D2"],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OK,
                },
            ]
        },
        TestSet {
            title: "obsolete_entry",
            entries: maplit::btreemap!{
                "A" => SynthEntry(5,1,1, (0, [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(1,1,2, (3, [(0,0),(0,0),(0,0)])),
            },
            tests: &[
                Test {
                    title: "",
                    claim_txg: 1,
                    hide_entries: &[],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN,
                }
            ],
        },
        TestSet {
            title: "invalid (txg,count) pair with (0,non-0)",
            entries: maplit::btreemap!{
                "A" => SynthEntry(1,1,1, (0, [(0,0),(0,23),(0,0)])),
            },
            tests: &[
                Test {
                    title: "",
                    claim_txg: 1,
                    hide_entries: &[],
                    expect: bindings::check_replayable_result_kind_CHECK_REPLAYABLE_INVALID_COUNT_EXPECTED_ZERO,
                },
            ],
        },
    ];

    for entryset in entrysets {
        for test in entryset.tests {
            println!("runing test {:?}: {:?}", entryset.title, test.title);
            let hide = HashSet::<&'static str>::from_iter(test.hide_entries.iter().cloned());

            // build replay btree
            let rns: Vec<replay_node_t> = entryset
                .entries
                .iter()
                .filter(|(name, _)| !hide.contains(*name))
                .map(|(_, se)| (*se).into())
                .collect();

            let mut bt = unsafe {
                let mut bt = std::mem::MaybeUninit::uninit();
                zfs_btree::create(
                    bt.as_mut_ptr(),
                    Some(bindings::zilpmem_replay_node_btree_cmp),
                    std::mem::size_of::<replay_node_t>().try_into().unwrap(),
                );
                let mut bt = bt.assume_init();
                rns.iter()
                    .for_each(|rn| zfs_btree::add(&mut bt, rn as *const _ as *const c_void));
                bt
            };

            println!("{:#?}", test);
            unsafe {
                let seq: Vec<bindings::zilpmem_replay_node_t> =
                    zfs_btree::iter(&mut bt).map(|p| *p).collect();
                println!("{:#?}", seq);

                let mut w = std::mem::MaybeUninit::uninit();
                let res =
                    bindings::zilpmem_check_replayable(&mut bt, w.as_mut_ptr(), test.claim_txg);
                assert_eq!(res.what, test.expect, "{:#?}", res);
                println!("{:#?}", res);
            }

            unsafe {
                zfs_btree::clear(&mut bt);
                zfs_btree::destroy(&mut bt);
            }
        }
    }
}

#[test]
fn zilpmem_replay_resume() {
    libzpool::init_once();

    use derivative::Derivative;
    use zilpmem::replay_node_t;

    #[derive(Derivative)]
    #[derivative(Debug)]
    struct Stage {
        title: &'static str,
        hide_entries: &'static [&'static str],
        stop_at: Option<&'static str>,
        #[derivative(Debug = "ignore")]
        check: fn(r: &bindings::check_replayable_result_t),
    }

    #[derive(Debug, Clone)]
    enum ReplayLogEntry {
        StartStage(&'static str),
        ReplayCallbackInvocation(String, ReplayResumeCallbackArgCopied),
    }

    impl ReplayLogEntry {
        fn replay_callback_invocation(&self) -> Option<(&String, &ReplayResumeCallbackArgCopied)> {
            match self {
                ReplayLogEntry::ReplayCallbackInvocation(a, b) => Some((a, b)),
                _ => None,
            }
        }
    }

    #[derive(Debug)]
    struct Test {
        title: &'static str,
        claim_txg: NonZeroU64,
        stages: Vec<Stage>,
        replay_log: Vec<ReplayLogEntry>,
        expect_replayed_entries: Vec<&'static str>,
    }

    #[derive(Debug)]
    struct TestSet {
        title: &'static str,
        entries: BTreeMap<&'static str, SynthEntry>,
        tests: Vec<Test>,
    }

    macro_rules! check {
        (OK) => {{ check!(IMPL          bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OK) }};
        (M_ENTRIES) => {{ check!(IMPL   bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES) }};
        (OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN) => {{ check!(IMPL bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN) }};
        ($e:expr) => {{ check!(IMPL $e)}};
        (IMPL $expect_result:expr) => {{
            |r| {
                assert_eq!( r.what, $expect_result)
            }
        }};
    }

    macro_rules! stages {
        (single, check=$check:ident, ) => { stages!(single, hide=&[], check=$check,) };
        (single, hide=$hide_entries:expr, check=$check:ident, ) => {{
            vec![Stage {
                title: "single",
                stop_at: None,
                hide_entries: $hide_entries,
                check: check!($check),
            }]
        }}
    }

    macro_rules! test {
        ($title:literal, claim_txg=$claim_txg:literal, stages=$stages:expr, expect_replay=$expect_replay:expr,) => {{
            Test {
                title: $title,
                replay_log: Default::default(),
                claim_txg: NonZeroU64::new($claim_txg).unwrap(),
                stages: $stages,
                expect_replayed_entries: $expect_replay,
            }
        }};
    }

    macro_rules! test_ok {
        ($title:literal, claim_txg=$claim_txg:literal, expect_replay=$expect_replay:expr,) => {{
            test!{$title,
                claim_txg=$claim_txg,
                stages=stages!(single, check=OK,),
                expect_replay=$expect_replay,
            }
        }};
    }

    let mut entrysets = vec![
        TestSet {
            title: "s1",
            entries: maplit::btreemap!{
                "A1" => SynthEntry(1,1,1,(0, [(0,0),(0,0),(0,0)])),
                "A2" => SynthEntry(1,1,2,(0, [(0,0),(0,0),(0,0)])),
                "B1" => SynthEntry(1,2,1,(1, [(1,2),(0,0),(0,0)])),
                "B2" => SynthEntry(1,2,2,(1, [(1,2),(0,0),(0,0)])),
                "B3" => SynthEntry(1,2,3,(1, [(1,2),(0,0),(0,0)])),
                "C1" => SynthEntry(1,3,1,(2, [(1,5),(0,0),(0,0)])),
                "C2" => SynthEntry(1,3,2,(2, [(1,5),(0,0),(0,0)])),
            },
            tests: vec![
                Test {
                    title: "all entries present at all times but crash at every entry",
                    replay_log: Default::default(),
                    claim_txg: NonZeroU64::new(1).unwrap(),
                    stages: {
                        let stop_at = &["A1", "A2", "B1", "B2", "B3", "C1", "C2"]; // FIXME get a ref to the entries list above
                        let mut stages = vec![];
                        for s in stop_at {
                            stages.push(Stage {
                                title: "stop", // FIXME include node
                                stop_at: Some(s),
                                hide_entries: &[], // TODO permute!
                                check: |r| {
                                    assert_eq!(r.what, bindings::check_replayable_result_kind_CHECK_REPLAYABLE_CALLBACK_STOPPED, "{:#?}", r);
                                },
                            })
                        }
                        stages
                    },
                    expect_replayed_entries: vec![
                        // FIXME get a ref to the entries list above
                        "A1",
                        "A2",
                        "B1",
                        "B2",
                        "B3",
                        "C1",
                        "C2",
                    ],
                },
                Test {
                    title: "entry reappearance in already-replayed section doesn't fix 'missing entries' error",
                    replay_log: Default::default(),
                    claim_txg: NonZeroU64::new(1).unwrap(),
                    stages: vec![
                        Stage{
                            title: "B2 missing",
                            hide_entries: &["B2"],
                            stop_at: None,
                            check: |r| {
                                // should fail with 'missing node' at C1
                                assert_eq!(r.what, bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES, "{:#?}", r);
                            }
                        },
                        Stage {
                            title: "B2 reappears",
                            hide_entries: &[],
                            stop_at: None,
                            check: |r| {
                                assert_eq!(r.what, bindings::check_replayable_result_kind_CHECK_REPLAYABLE_MISSING_ENTRIES, "{:#?}", r);
                            }
                        }
                    ],
                    expect_replayed_entries: vec![
                        "A1",
                        "A2",
                        "B1",
                        "B3",
                    ],
                },
                Test {
                    title: "entry reappearance at the end is fixed silently",
                    replay_log: Default::default(),
                    claim_txg: NonZeroU64::new(1).unwrap(),
                    stages: vec![
                        Stage{
                            title: "stop at A2",
                            hide_entries: &["B2", "C1"],
                            stop_at: Some("A2"),
                            check: |r| {
                                assert_eq!(r.what, bindings::check_replayable_result_kind_CHECK_REPLAYABLE_CALLBACK_STOPPED, "{:#?}", r);
                            }
                        },
                        Stage {
                            title: "now B2 and C1 have re-appeared, we should be able to replay to the end",
                            hide_entries: &[],
                            stop_at: None,
                            check: |r| {
                                assert_eq!(r.what, bindings::check_replayable_result_kind_CHECK_REPLAYABLE_OK, "{:#?}", r);
                            }

                        },
                    ],
                    expect_replayed_entries: vec![
                            // FIXME get a ref to the entries list above
                            "A1",
                            "A2",
                            "B1",
                            "B2",
                            "B3",
                            "C1",
                            "C2",
                    ]
                }
            ],
        },
        TestSet {
            title: "what happens if claim_txg > 1",
            entries: maplit::btreemap! {
                "A" => SynthEntry(1,1,1, (0, [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(2,1,2, (0, [(0,0),(0,0),(0,0)])),
                "C" => SynthEntry(3,1,3, (0, [(0,0),(0,0),(0,0)])),
                "D" => SynthEntry(4,1,4, (0, [(0,0),(0,0),(0,0)])),
                "E" => SynthEntry(5,1,5, (0, [(0,0),(0,0),(0,0)])),
                "F" => SynthEntry(6,1,6, (0, [(0,0),(0,0),(0,0)])),
                "G" => SynthEntry(7,1,7, (0, [(0,0),(0,0),(0,0)])),
                "H" => SynthEntry(8,1,8, (0, [(0,0),(0,0),(0,0)])),
                "I" => SynthEntry(9,1,9, (0, [(0,0),(0,0),(0,0)])),
            },
            tests: vec![
                test_ok! {
                    "claim_txg=1 => all entries",
                    claim_txg = 1,
                    expect_replay = vec!["A","B","C","D","E","F","G","H","I"],
                },
                test_ok! {
                    "claim_txg=2 => not A",
                    claim_txg = 2,
                    expect_replay = vec!["B","C","D","E","F","G","H","I"],
                },
                test_ok! {
                    "claim_txg=9 => just I",
                    claim_txg = 9,
                    expect_replay = vec![ "I" ],
                },
                test_ok! {
                    "claim_txg=10 => no entries",
                    claim_txg = 10,
                    expect_replay = vec![ ],
                },
            ],
        },
        TestSet {
            title: "zig-zag",
            entries: maplit::btreemap! {
                "A" => SynthEntry(3,3,1, (2, [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(4,4,1, (3, [(3,1),(0,0),(0,0)])),
                "C" => SynthEntry(3,5,1, (4, [(4,1),(3,1),(0,0)])),
                "D" => SynthEntry(5,6,1, (5, [(4,1),(3,2),(0,0)])),
            },
            tests: vec![
                test_ok! {
                    "claim_txg lower than min txg of entries",
                    claim_txg = 2,
                    expect_replay = vec!["A", "B", "C", "D"],
                },
                test_ok! {
                    "claim_txg is min txg of entries",
                    claim_txg = 3,
                    expect_replay = vec!["A", "B", "C", "D"],
                },
                test_ok! {
                    "claim_txg cuts off min txg",
                    claim_txg=4,
                    expect_replay = vec!["B", "D"],
                },
                test_ok! {
                    "claim_txg cuts of more txgs",
                    claim_txg = 5,
                    expect_replay = vec!["D"],
                },
                test_ok! {
                    "claim_txg cuts of all txgs",
                    claim_txg = 6,
                    expect_replay = vec![],
                },
                // let's try a bunch of variations where we hide entries
                test! {
                    "hiding entry obsoleted by claim_txg (1)",
                    claim_txg = 4,
                    stages = stages!(single, hide=&["A"], check=OK,),
                    expect_replay = vec!["B", "D"],
                },
                test! {
                    "hiding entry obsoleted by claim_txg (2)",
                    claim_txg = 4,
                    stages = stages!(single, hide=&["C"], check=OK,),
                    expect_replay = vec!["B", "D"],
                },
                test! {
                    "hiding entry that is required after we moved to the next txg + gen",
                    claim_txg = 3,
                    stages = stages!(single, hide=&["C"], check=M_ENTRIES,),
                    expect_replay = vec!["A", "B"],
                },
                test! {
                    "hiding entry that is required but the entryis in a txg > claim_txg",
                    claim_txg = 4,
                    stages = stages!(single, hide=&["B"], check=M_ENTRIES,),
                    expect_replay = vec![],
                },
            ],
        },
        TestSet {
            title: "v shape different gens",
            entries: maplit::btreemap! {
                // FIXME test that this is something we'd emit
                "A" => SynthEntry(3,10,1, (0, [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(4,11,1, (1, [(3,1),(0,0),(0,0)])),
                "C" => SynthEntry(3,12,1, (2, [(4,1),(3,1),(0,0)])),
            },
            tests: vec![
                test! {
                    "hiding B => missing entry",
                    claim_txg = 1,
                    stages = stages!(single, hide=&["B"], check=M_ENTRIES,),
                    expect_replay = vec!["A"],
                }
            ],
        },
        TestSet {
            title: "I- shape",
            entries: maplit::btreemap! {
                // FIXME test that this is something we'd emit
                "A" => SynthEntry(3,10,1, (0,  [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(4,10,2, (0,  [(0,0),(0,0),(0,0)])),
                "C" => SynthEntry(5,10,3, (0,  [(0,0),(0,0),(0,0)])),
                "D" => SynthEntry(4,11,1, (10, [(5,1),(4,1),(3,1)]))
            },
            tests: vec![
                test! {
                    "tail truncation ok",
                    claim_txg = 1,
                    stages = stages!(single, hide=&["D"], check=OK,),
                    expect_replay = vec!["A", "B", "C"],
                },
                test! {
                    "A missing detected but the remainder of its gen is replayed",
                    claim_txg  = 1,
                    stages = stages!(single, hide=&["A"], check=M_ENTRIES,),
                    expect_replay = vec!["B", "C"],
                },
                test! {
                    "B missing detected but the remainder of its gen is replayed",
                    claim_txg  = 1,
                    stages = stages!(single, hide=&["B"], check=M_ENTRIES,),
                    expect_replay = vec!["A", "C"],
                },
                test! {
                    "C missing detected but the remainder of its gen is replayed",
                    claim_txg  = 1,
                    stages = stages!(single, hide=&["C"], check=M_ENTRIES,),
                    expect_replay = vec!["A", "B"],
                },
            ],
        },
        TestSet {
            title: "entries that returned PRB_WRITE_OBSOLETE cause replay error",
            entries: maplit::btreemap! {
                // all same gen so we don't hit M_ENTRIES
                "A" => SynthEntry(3,10,1, (0, [(0,0),(0,0),(0,0)])),
                "B" => SynthEntry(6,10,2, (0, [(0,0),(0,0),(0,0)])),
                "C" => SynthEntry(3,10,3, (0, [(0,0),(0,0),(0,0)])),
            },
            tests: vec![
                test! {
                    "",
                    claim_txg = 1,
                    stages = stages!(single, check=OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN,),
                    expect_replay = vec!["A", "B"],
                }
            ],
        }
    ];

    for entryset in &mut entrysets {
        for test in &mut entryset.tests {
            println!("runing test {:?}: {:?}", entryset.title, test.title);

            println!("test:\n{:#?}", test);

            // Initialize replay state. The stages share the replay state.
            let mut st = unsafe {
                let mut st = std::mem::MaybeUninit::uninit();
                zilpmem::replay_state_init(st.as_mut_ptr(), test.claim_txg.get());
                st.assume_init()
            };

            // have to borrow it it before borrowing &test.stages in the iterator
            let replay_log = &mut test.replay_log;
            for stage in &test.stages {
                replay_log.push(ReplayLogEntry::StartStage(stage.title));

                use std::iter::FromIterator;
                let hide = HashSet::<&'static str>::from_iter(stage.hide_entries.iter().cloned());

                let mut name_box_ptrs = Vec::new();

                // build replay btree
                // we track the name in the pmem ptr value
                // FIXME: introduce an 'argdata' field of some sort so that
                // we don't have to rely on this field (bad for production sizing though...)
                let rns: Vec<replay_node_t> = entryset
                    .entries
                    .iter()
                    .filter(|(name, _)| !hide.contains(*name))
                    .map(|(name, se)| {
                        let mut rn: replay_node_t = (*se).into();
                        let nb: Box<String> = Box::new((*name).to_owned());
                        let nbp = Box::into_raw(nb);
                        name_box_ptrs.push(nbp);
                        rn.rn_pmem_ptr = nbp as *const u8;
                        rn
                    })
                    .collect();

                let mut bt = unsafe {
                    let mut bt = std::mem::MaybeUninit::uninit();
                    zfs_btree::create(
                        bt.as_mut_ptr(),
                        Some(bindings::zilpmem_replay_node_btree_cmp),
                        std::mem::size_of::<replay_node_t>().try_into().unwrap(),
                    );
                    let mut bt = bt.assume_init();
                    rns.iter()
                        .for_each(|rn| zfs_btree::add(&mut bt, rn as *const _ as *const c_void));
                    bt
                };

                unsafe {
                    let seq: Vec<bindings::zilpmem_replay_node_t> =
                        zfs_btree::iter(&mut bt).map(|p| *p).collect();
                    println!("{:#?}", seq);

                    let mut w = std::mem::MaybeUninit::uninit();
                    let res = zilpmem::replay_resume_rust(
                        &mut bt,
                        w.as_mut_ptr(),
                        &mut st,
                        |ref arg| {
                            /* we set this up above */
                            let nbp = (&*arg.node).rn_pmem_ptr as *const String;
                            let name = &*nbp;
                            println!("replaying {:?}, {:?}", name, arg);
                            replay_log.push(ReplayLogEntry::ReplayCallbackInvocation(
                                name.to_owned(),
                                arg.into(),
                            ));
                            let stop_here = stage
                                .stop_at
                                .as_ref()
                                .map(|stop_node_name| stop_node_name == name)
                                .unwrap_or(false);
                            let ret = if stop_here {
                                bindings::zilpmem_replay_resume_cb_result_ZILPMEM_REPLAY_RESUME_CB_RESULT_STOP
                            } else {
                                bindings::zilpmem_replay_resume_cb_result_ZILPMEM_REPLAY_RESUME_CB_RESULT_NEXT
                            };
                            println!("returning callback result {:#?}", ret);
                            ret
                        },
                    );
                    (stage.check)(&res);
                }

                name_box_ptrs.into_iter().for_each(|p| {
                    let nb: Box<String> = unsafe { Box::from_raw(p) };
                    drop(nb);
                });

                unsafe {
                    zfs_btree::clear(&mut bt);
                    zfs_btree::destroy(&mut bt);
                }
            }

            let replayed_entries: Vec<(&String, &ReplayResumeCallbackArgCopied)> = test
                .replay_log
                .iter()
                .filter_map(|le| le.replay_callback_invocation())
                .collect();

            let replayed_entry_names: Vec<&str> = replayed_entries
                .iter()
                .map(|(name, _)| name.as_str())
                .collect();

            assert_eq!(replayed_entry_names, test.expect_replayed_entries);
        }
    }
}

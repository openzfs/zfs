//! This module provides a mechanism to have the Agent process randmly exit at
//! certain "interesting" points, to test recovery on restart.  To use it, set
//! the "die_mtbf_secs" tunable to the desired mean time between failures, in
//! seconds.  A time point between 0 and 2x the configured time will be selected
//! as the amount of time to run before dying.  At that point, a random call
//! site of `maybe_die_with()` will be selected to exit the process.
//!
//! Note that each *call site* (source file, line, column) is equally likely to
//! die, not each *call* (invocation of maybe_die_with()).  For example,
//! maybe_die_with() is called 1000x/sec from one call site and 1x/sec from
//! another call site, we will be equally likely to terminate via each of the 2
//! call sites.  Therefore you don't need to worry about adding a high-frequency
//! caller and having it "always" die on that caller.

use crate::get_tunable;
use backtrace::Backtrace;
use lazy_static::lazy_static;
use log::*;
use std::{
    collections::HashSet,
    ffi::OsStr,
    fmt::Display,
    panic::Location,
    path::Path,
    sync::RwLock,
    time::{Duration, Instant},
};

lazy_static! {
    // RUN_TIME is a random amount between 0 and 2x the configured MTBF (Mean
    // Time Between Failures)
    // XXX use humantime::parse_duration so it can be hours, etc?
    static ref RUN_TIME: Option<Duration> = get_tunable("die_mtbf_secs", None)
        .map(|secs: f64| Duration::from_secs_f64(secs * rand::random::<f64>() * 2.0));
    static ref LOCATIONS: RwLock<HashSet<&'static Location<'static>>> = Default::default();
    static ref BEGIN: Instant = Instant::now();
    // tunable should be the "basename" (e.g. zettacache.rs)
    static ref DIE_FILE: Option<String> = get_tunable("die_file", None);
    static ref DIE_LINE: Option<u32> = get_tunable("die_line", None);
}

// Instead of taking a string (or Display) to print, this takes a function which
// returns the Display.  This is so that the caller don't have the cost of
// generating the string on every call, only when we're actually dying.
// track_caller ensures that Location::caller() will capture the call site of this function
#[track_caller]
pub fn maybe_die_with<M, F>(f: F)
where
    F: FnOnce() -> M,
    M: Display,
{
    if let Some(run_time) = *RUN_TIME {
        let location = Location::caller();
        if !LOCATIONS.read().unwrap().contains(location) {
            LOCATIONS.write().unwrap().insert(location);
        }
        if BEGIN.elapsed() >= run_time {
            // Check if this is the location chosen to die.  We declare
            // DIE_LOCATION here so that we're sure to not evaluate it until
            // RUN_TIME has elapsed, so that LOCATIONS has been filled in.
            lazy_static! {
                static ref DIE_LOCATION: Option<&'static Location<'static>> = {
                    let possible_locations = LOCATIONS
                        .read()
                        .unwrap()
                        .iter()
                        .filter(|location| {
                            DIE_LINE.map_or(true, |line| location.line() == line)
                                && DIE_FILE.as_ref().map_or(true, |file| {
                                    Path::new(location.file()).file_name().unwrap()
                                        == OsStr::new(file)
                                })
                        })
                        .copied()
                        .collect::<Vec<_>>();

                    if possible_locations.is_empty() {
                        warn!(
                            "after running {} seconds, no valid site to die; file:{:?} line:{:?}",
                            RUN_TIME.unwrap().as_secs(),
                            *DIE_FILE,
                            *DIE_LINE,
                        );
                        None
                    } else {
                        let die_location =
                            possible_locations[rand::random::<usize>() % possible_locations.len()];
                        warn!(
                            "after running {} seconds, selected site to die: {}",
                            RUN_TIME.unwrap().as_secs(),
                            die_location
                        );
                        Some(die_location)
                    }
                };
            }
            if Some(location) == *DIE_LOCATION {
                let msg = f();
                let backtrace = Backtrace::new();
                warn!("exiting to test failure handling: {} {:?}", msg, backtrace);
                println!("exiting to test failure handling: {} {:?}", msg, backtrace);
                std::process::exit(0);
            }
        }
    }
}

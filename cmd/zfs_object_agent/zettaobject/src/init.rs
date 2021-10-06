use crate::pool_destroy;
use crate::public_connection::PublicServerState;
use crate::root_connection::RootServerState;
use fs2::FileExt;
use lazy_static::lazy_static;
use log::*;
use log4rs::append::console::ConsoleAppender;
use log4rs::append::file::FileAppender;
use log4rs::config::Logger;
use log4rs::config::{Appender, Config, Root};
use log4rs::encode::pattern::PatternEncoder;
use std::fs::OpenOptions;
use std::io::{Read, Write};
use std::mem;
use std::process;
use zettacache::ZettaCache;

lazy_static! {
    static ref LOG_PATTERN: String = "[{d(%Y-%m-%d %H:%M:%S%.3f)}][{t}][{l}] {m}{n}".to_string();
}

pub fn get_logging_level(verbosity: u64) -> LevelFilter {
    match verbosity {
        0 => LevelFilter::Warn,
        1 => LevelFilter::Info,
        2 => LevelFilter::Debug,
        _ => LevelFilter::Trace,
    }
}

fn setup_console_logging(verbosity: u64) {
    let config = Config::builder()
        .appender(
            Appender::builder().build(
                "stdout",
                Box::new(
                    ConsoleAppender::builder()
                        .encoder(Box::new(PatternEncoder::new(&*LOG_PATTERN)))
                        .build(),
                ),
            ),
        )
        // rusoto_core::request is very chatty when set to debug. So, set it to info.
        .logger(Logger::builder().build("rusoto_core::request", LevelFilter::Info))
        .build(
            Root::builder()
                .appender("stdout")
                .build(get_logging_level(verbosity)),
        )
        .unwrap();

    log4rs::init_config(config).unwrap();
}

fn setup_logfile(verbosity: u64, logfile: &str) {
    let config = Config::builder()
        .appender(
            Appender::builder().build(
                "logfile",
                Box::new(
                    FileAppender::builder()
                        .encoder(Box::new(PatternEncoder::new(&*LOG_PATTERN)))
                        .build(logfile)
                        .unwrap(),
                ),
            ),
        )
        // rusoto_core::request is very chatty when set to debug. So, set it to info.
        .logger(Logger::builder().build("rusoto_core::request", LevelFilter::Info))
        .build(
            Root::builder()
                .appender("logfile")
                .build(get_logging_level(verbosity)),
        )
        .unwrap();

    log4rs::init_config(config).unwrap();
}

pub fn setup_logging(verbosity: u64, file_name: Option<&str>, log_config: Option<&str>) {
    match log_config {
        Some(config) => {
            log4rs::init_file(config, Default::default()).unwrap();
        }
        None => {
            match file_name {
                Some(logfile) => {
                    setup_logfile(verbosity, logfile);
                }
                None => {
                    /*
                     * When neither the log_config nor a log file is specified
                     * log to console.
                     */
                    setup_console_logging(verbosity);
                }
            }
        }
    };
}

fn lock_socket_dir(socket_dir: &str) {
    let lock_file = format!("{}/zoa.lock", socket_dir);
    match OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(&lock_file)
    {
        Ok(mut file) => {
            match file.try_lock_exclusive() {
                Ok(_) => {
                    let pid = format!("{}", process::id());
                    file.set_len(0).unwrap();
                    file.write_all(pid.as_bytes()).unwrap();

                    /*
                     * The exclusive lock on the file is held until it is closed. Since we want to hold that lock until
                     * this process exits, we need to hold on to the file. But since we don't need to access the file
                     * anymore, we just "forget" about the file without running its destructor.
                     */
                    mem::forget(file);
                }
                Err(error) => {
                    let mut buffer = String::new();
                    file.read_to_string(&mut buffer).unwrap();
                    error!(
                        "Another zfs_object_agent process with pid {} is running. Error: {:?}",
                        buffer, error
                    );
                    std::process::exit(2);
                }
            }
        }
        Err(_) => {
            error!("Failed to create lock file {}", &lock_file);
            std::process::exit(1);
        }
    }
}

pub fn start(socket_dir: &str, cache_path: Option<&str>) {
    /*
     * Take an exclusive lock on a lock file. This prevents multiple agent
     * processes from operating out of the same socket_dir.
     */
    lock_socket_dir(socket_dir);

    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .thread_name("zoa")
        .build()
        .unwrap()
        .block_on(async move {
            // Kick off zpool destroy tasks.
            pool_destroy::init_pool_destroyer(socket_dir).await;

            PublicServerState::start(socket_dir);

            let cache = match cache_path {
                Some(path) => Some(ZettaCache::open(path).await),
                None => None,
            };

            RootServerState::start(socket_dir, cache);

            // keep the process from exiting
            let () = futures::future::pending().await;
        });
}

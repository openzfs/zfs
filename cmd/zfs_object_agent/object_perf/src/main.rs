use clap::AppSettings;
use clap::Arg;
use clap::SubCommand;
use std::time::Duration;
use uuid::Uuid;
use zettaobject::ObjectAccess;
mod s3perf;

const ENDPOINT: &str = "https://s3-us-west-2.amazonaws.com";
const REGION: &str = "us-west-2";
const BUCKET_NAME: &str = "cloudburst-data-2";

#[tokio::main]
async fn main() {
    let matches = clap::App::new("zfs_object_perf")
        .setting(AppSettings::SubcommandRequiredElseHelp)
        .about("ZFS object storage performance tests")
        .version("1.0")
        .arg(
            Arg::with_name("endpoint")
                .short("e")
                .long("endpoint")
                .help("S3 endpoint")
                .takes_value(true)
                .default_value(ENDPOINT),
        )
        .arg(
            Arg::with_name("region")
                .short("r")
                .long("region")
                .help("S3 region")
                .takes_value(true)
                .default_value(REGION),
        )
        .arg(
            Arg::with_name("bucket")
                .short("b")
                .long("bucket")
                .help("S3 bucket")
                .takes_value(true)
                .default_value(BUCKET_NAME),
        )
        .arg(
            Arg::with_name("profile")
                .short("p")
                .long("profile")
                .help("credentials profile")
                .takes_value(true)
                .default_value("default"),
        )
        .arg(
            Arg::with_name("object-size")
                .short("s")
                .long("object-size")
                .help("Object size in KiB")
                .takes_value(true)
                .default_value("1024"),
        )
        .arg(
            Arg::with_name("qdepth")
                .short("q")
                .long("qdepth")
                .help("number of concurrent GET/PUT operations")
                .takes_value(true)
                .default_value("10"),
        )
        .arg(
            Arg::with_name("runtime")
                .short("t")
                .long("time")
                .help("How long to run the test (in seconds)")
                .takes_value(true)
                .default_value("30"),
        )
        .arg(
            Arg::with_name("verbosity")
                .short("v")
                .multiple(true)
                .help("Sets the level of logging verbosity"),
        )
        .arg(
            Arg::with_name("output-file")
                .short("o")
                .long("output-file")
                .value_name("FILE")
                .help("File to log output to")
                .takes_value(true)
                .default_value("/var/tmp/perflog"),
        )
        .arg(
            Arg::with_name("log-config")
                .short("l")
                .long("log-config")
                .value_name("FILE")
                .help("Configuration yaml file logging")
                .takes_value(true),
        )
        .subcommand(SubCommand::with_name("write").about("write test"))
        .subcommand(SubCommand::with_name("read").about("read test"))
        .get_matches();

    zettaobject::init::setup_logging(
        matches.occurrences_of("verbosity"),
        matches.value_of("output-file"),
        matches.value_of("log-config"),
    );

    // Command line parameters
    let endpoint = matches.value_of("endpoint").unwrap();
    let region_str = matches.value_of("region").unwrap();
    let bucket_name = matches.value_of("bucket").unwrap();
    let profile = matches.value_of("profile").unwrap();
    let objsize_bytes: u64 = matches
        .value_of("object-size")
        .unwrap()
        .parse::<u64>()
        .unwrap()
        * 1024;
    let qdepth: u64 = matches.value_of("qdepth").unwrap().parse().unwrap();
    let duration = Duration::from_secs(matches.value_of("runtime").unwrap().parse().unwrap());

    println!(
        "endpoint: {}, region: {}, bucket: {} profile: {}",
        endpoint, region_str, bucket_name, profile
    );

    let object_access: ObjectAccess = ObjectAccess::new(
        endpoint,
        region_str,
        bucket_name,
        Some(profile.to_owned()),
        false,
    );

    let key_prefix = format!("zfs_object_perf/{}/", Uuid::new_v4());
    println!("Using prefix: '{}'", key_prefix);
    match matches.subcommand() {
        ("write", Some(_matches)) => {
            s3perf::write_test(&object_access, key_prefix, objsize_bytes, qdepth, duration)
                .await
                .unwrap();
        }
        ("read", Some(_matches)) => {
            s3perf::read_test(&object_access, key_prefix, objsize_bytes, qdepth, duration)
                .await
                .unwrap();
        }
        _ => {
            matches.usage();
        }
    };
}

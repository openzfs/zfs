use chrono::prelude::*;
use chrono::DateTime;
use clap::Arg;
use clap::SubCommand;
use client::Client;
use futures::stream::StreamExt;
use nvpair::*;
use rand::prelude::*;
use rusoto_core::ByteStream;
use rusoto_credential::ChainProvider;
use rusoto_credential::DefaultCredentialsProvider;
use rusoto_credential::InstanceMetadataProvider;
use rusoto_credential::ProfileProvider;
use rusoto_credential::ProvideAwsCredentials;
use rusoto_s3::*;
use std::collections::BTreeSet;
use std::error::Error;
use std::fs;
use std::fs::File;
use std::io::prelude::*;
use std::io::Read;
use std::time::{Duration, Instant};
use tokio::io::AsyncReadExt;
use zettacache::base_types::*;
use zettaobject::base_types::*;
use zettaobject::ObjectAccess;
use zettaobject::Pool;
mod client;

const ENDPOINT: &str = "https://s3-us-west-2.amazonaws.com";
const REGION: &str = "us-west-2";
const BUCKET_NAME: &str = "cloudburst-data-2";
const POOL_NAME: &str = "testpool";
const POOL_GUID: u64 = 1234;

async fn do_rusoto_provider<P>(credentials_provider: P, file: &str)
where
    P: ProvideAwsCredentials + Send + Sync + 'static,
{
    let http_client = rusoto_core::HttpClient::new().unwrap();
    let client = S3Client::new_with(
        http_client,
        credentials_provider,
        rusoto_core::Region::UsWest2,
    );

    let content = "I want to go to S3".as_bytes().to_vec();

    println!("putting {}", file);
    let req = PutObjectRequest {
        bucket: BUCKET_NAME.to_string(),
        key: file.to_string(),
        body: Some(ByteStream::from(content)),
        ..Default::default()
    };
    match client.put_object(req).await {
        Ok(result) => {
            println!("Success {:?}", result);
        }
        Err(result) => {
            println!("Failure {:?}", result);
        }
    }
}

async fn do_rusoto_role() -> Result<(), Box<dyn Error>> {
    do_rusoto_provider(
        InstanceMetadataProvider::new(),
        "test/InstanceMetadataProvider.txt",
    )
    .await;

    do_rusoto_provider(ProfileProvider::new().unwrap(), "test/ProfileProvider.txt").await;
    do_rusoto_provider(ChainProvider::new(), "test/ChainProvider.txt").await;
    do_rusoto_provider(
        DefaultCredentialsProvider::new().unwrap(),
        "test/DefaultCredentialsProvider.txt",
    )
    .await;

    Ok(())
}

async fn do_s3_rusoto() -> Result<(), Box<dyn Error>> {
    let client = S3Client::new(rusoto_core::Region::UsWest2);

    let key = "mahrens/test.file2";

    println!("getting {}", key);
    let req = GetObjectRequest {
        bucket: BUCKET_NAME.to_string(),
        key: key.to_string(),
        ..Default::default()
    };
    let res = client.get_object(req).await?;
    let mut s = String::new();
    res.body
        .unwrap()
        .into_async_read()
        .read_to_string(&mut s)
        .await
        .unwrap();
    println!("object contents = {}", s);

    let content = "I want to go to S3".as_bytes().to_vec();
    println!("putting {}", key);
    let req = PutObjectRequest {
        bucket: BUCKET_NAME.to_string(),
        key: key.to_string(),
        body: Some(ByteStream::from(content)),
        ..Default::default()
    };
    client.put_object(req).await?;

    Ok(())
}

fn do_btree() {
    let mut bt: BTreeSet<[u64; 8]> = BTreeSet::new();
    let mut rng = rand::thread_rng();
    let n = 10000000;
    for _ in 0..n {
        bt.insert(rng.gen());
    }
    println!("added {} items to btree", n);
    std::thread::sleep(Duration::from_secs(1000));
}

async fn do_create() -> Result<(), Box<dyn Error>> {
    let mut client = Client::connect().await;
    let endpoint = ENDPOINT;
    let region = REGION;
    let bucket_name = BUCKET_NAME;
    let pool_name = POOL_NAME;
    let guid = PoolGuid(POOL_GUID);

    client
        .create_pool(region, endpoint, bucket_name, guid, pool_name)
        .await;
    client.get_next_response().await;

    Ok(())
}

async fn do_write() -> Result<(), Box<dyn Error>> {
    let guid = PoolGuid(1234);
    let (mut client, next_txg, mut next_block) = setup_client(guid).await;

    let begin = Instant::now();
    let n = 5200;

    client.begin_txg(guid, next_txg).await;

    let task = client.get_responses_initiate(n);

    for _ in 0..n {
        let mut data: Vec<u8> = Vec::new();
        let mut rng = thread_rng();
        let len = rng.gen::<u32>() % 100;
        for _ in 0..len {
            data.push(rng.gen());
        }
        //println!("requesting write of {}B", len);
        client.write_block(guid, next_block, &data).await;
        //println!("writing {}B to {:?}...", len, id);
        next_block = BlockId(next_block.0 + 1);
    }
    client.flush_writes(guid).await;
    client.get_responses_join(task).await;

    client.end_txg(guid, &[]).await;
    client.get_next_response().await;

    println!("wrote {} blocks in {}ms", n, begin.elapsed().as_millis());

    Ok(())
}

async fn setup_client(guid: PoolGuid) -> (Client, Txg, BlockId) {
    let mut client = Client::connect().await;

    let bucket_name = BUCKET_NAME;
    let endpoint = ENDPOINT;
    let region = REGION;

    client.open_pool(region, endpoint, bucket_name, guid).await;

    let nvl = client.get_next_response().await;
    let txg = Txg(nvl.lookup_uint64("next txg").unwrap());
    let block = BlockId(nvl.lookup_uint64("next block").unwrap());

    (client, txg, block)
}

async fn do_read() -> Result<(), Box<dyn Error>> {
    let guid = PoolGuid(1234);
    let (mut client, _, _) = setup_client(guid).await;

    let max = 1000;
    let begin = Instant::now();
    let n = 50;

    let task = client.get_responses_initiate(n);

    for _ in 0..n {
        let id = BlockId((thread_rng().gen::<u64>() + 1) % max);
        client.read_block(guid, id).await;
    }

    client.get_responses_join(task).await;

    println!("read {} blocks in {}ms", n, begin.elapsed().as_millis());

    Ok(())
}

async fn do_free() -> Result<(), Box<dyn Error>> {
    let guid = PoolGuid(1234);
    let (mut client, mut next_txg, mut next_block) = setup_client(guid).await;

    // write some blocks, which we will then free some of

    client.begin_txg(guid, next_txg).await;
    next_txg = Txg(next_txg.0 + 1);

    let num_writes: usize = 10000;
    let task = client.get_responses_initiate(num_writes);
    let mut ids = Vec::new();

    for _ in 0..num_writes {
        let mut data: Vec<u8> = Vec::new();
        let mut rng = thread_rng();
        let len = rng.gen::<u32>() % 100;
        for _ in 0..len {
            data.push(rng.gen());
        }
        //println!("requesting write of {}B", len);
        client.write_block(guid, next_block, &data).await;
        //println!("writing {}B to {:?}...", len, id);
        ids.push(next_block);
        next_block = BlockId(next_block.0 + 1);
    }
    client.flush_writes(guid).await;
    client.get_responses_join(task).await;

    client.end_txg(guid, &[]).await;
    client.get_next_response().await;

    // free half the blocks, randomly selected
    client.begin_txg(guid, next_txg).await;

    for i in rand::seq::index::sample(&mut thread_rng(), ids.len(), ids.len() / 2) {
        client.free_block(guid, ids[i]).await;
    }
    client.end_txg(guid, &[]).await;
    client.get_next_response().await;

    Ok(())
}

fn get_file_as_byte_vec(filename: &str) -> Vec<u8> {
    let mut f = File::open(filename).expect("no file found");
    let metadata = fs::metadata(filename).expect("unable to read metadata");
    let mut buffer = vec![0; metadata.len() as usize];
    f.read_exact(&mut buffer).expect("buffer overflow");

    buffer
}

fn write_file_as_bytes(filename: &str, contents: &[u8]) {
    let mut f = File::create(filename).unwrap();
    f.write_all(contents).unwrap();
}

fn do_nvpair() {
    let buf = get_file_as_byte_vec("/etc/zfs/zpool.cache");
    let nvp = &mut NvList::try_unpack(buf.as_slice()).unwrap();
    //let nvp = &mut NvListRef::unpack(&buf[..]).unwrap();

    nvp.insert("new int", &5).unwrap();

    let vec: Vec<u8> = vec![1, 2, 3];
    nvp.insert("new uint8 array", vec.as_slice()).unwrap();

    println!("{:#?}", nvp);

    let newbuf = nvp.pack(NvEncoding::Native).unwrap();
    write_file_as_bytes("./zpool.cache.rust", &newbuf);
}

fn has_expired(mod_time: &DateTime<FixedOffset>, min_age: Duration) -> bool {
    let age = Local::now().signed_duration_since(*mod_time);
    min_age == Duration::from_secs(0) || age > chrono::Duration::from_std(min_age).unwrap()
}

async fn print_super(
    object_access: &ObjectAccess,
    pool_key: &str,
    mod_time: &DateTime<FixedOffset>,
) {
    print!("{:30} {:40}", mod_time, pool_key);
    let split: Vec<&str> = pool_key.rsplitn(3, '/').collect();
    let guid_str: &str = split[1];
    if let Ok(guid64) = str::parse::<u64>(guid_str) {
        let guid = PoolGuid(guid64);
        match Pool::get_config(object_access, guid).await {
            Ok(pool_config) => {
                let name = pool_config.lookup_string("name").unwrap();
                let hostname = pool_config.lookup_string("hostname").unwrap();
                println!(
                    "\t{:20} {}",
                    name.to_str().unwrap(),
                    hostname.to_str().unwrap()
                );
            }
            Err(_e) => {
                println!("\t-unknown format-");
            }
        }
    }
}

async fn find_old_pools(object_access: &ObjectAccess, min_age: Duration) -> Vec<String> {
    let pool_keys: Vec<String> = object_access
        .list_prefixes("zfs/".to_string())
        .collect()
        .await;
    let mut vec = Vec::new();
    for pool_key in pool_keys {
        match object_access
            .head_object(format!("{}super", pool_key))
            .await
        {
            Some(output) => {
                let mod_time =
                    DateTime::parse_from_rfc2822(output.last_modified.as_ref().unwrap()).unwrap();
                print_super(object_access, &pool_key, &mod_time).await;
                if has_expired(&mod_time, min_age) {
                    vec.push(pool_key);
                } else {
                    println!(
                        "Skipping pool as it is not {} days old.",
                        min_age.as_secs() / (60 * 60 * 24)
                    );
                }
            }
            None => {
                println!("didn't find super object for {}", pool_key);
            }
        }
    }
    vec
}

async fn do_list_pools(
    object_access: &ObjectAccess,
    list_all_objects: bool,
) -> Result<(), Box<dyn Error>> {
    for pool_key in find_old_pools(object_access, Duration::from_secs(0)).await {
        // Lookup all objects in the pool.
        if list_all_objects {
            object_access
                .list_objects(pool_key, None, false)
                .for_each(|object| async move { println!("    {}", object) })
                .await;
        }
    }
    Ok(())
}

async fn do_destroy_old_pools(
    object_access: &ObjectAccess,
    min_age: Duration,
) -> Result<(), Box<dyn Error>> {
    for pool_keys in find_old_pools(object_access, min_age).await {
        object_access
            .delete_objects(object_access.list_objects(pool_keys, None, false))
            .await;
    }
    Ok(())
}

fn get_object_access(
    endpoint: &str,
    region: &str,
    bucket: &str,
    profile: &str,
    aws_access_key_id: Option<&str>,
    aws_secret_access_key: Option<&str>,
) -> ObjectAccess {
    match aws_access_key_id {
        None => ObjectAccess::new(endpoint, region, bucket, Some(profile.to_owned()), false),
        Some(access_id) => {
            // If access_id is specified, aws_secret_access_key should also be specified.
            let secret_key = aws_secret_access_key.unwrap();

            let client =
                ObjectAccess::get_client_with_creds(endpoint, region, access_id, secret_key);

            ObjectAccess::from_client(client, bucket, false, endpoint, region)
        }
    }
}

// Test by writing and deleting an object.
async fn do_test_connectivity(object_access: &ObjectAccess) {
    let num = thread_rng().gen::<u64>();
    let file = format!("test/test_connectivity_{}", num);
    let content = "test connectivity to S3".as_bytes().to_vec();

    object_access.put_object(file.clone(), content).await;
    object_access.delete_object(file).await;
}

async fn test_connectivity(object_access: &ObjectAccess) -> Result<(), Box<dyn Error>> {
    std::process::exit(
        match tokio::time::timeout(Duration::from_secs(30), do_test_connectivity(object_access))
            .await
        {
            Err(_) => {
                eprintln!("Connectivity test failed.");
                -1
            }
            Ok(_) => {
                println!("Connectivity test succeeded.");
                0
            }
        },
    );
}

#[tokio::main]
async fn main() {
    let matches = clap::App::new("zoa_test")
        .about("ZFS object agent test")
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
            Arg::with_name("aws_access_key_id")
                .short("i")
                .long("aws_access_key_id")
                .takes_value(true)
                .help("AWS access key id"),
        )
        .arg(
            Arg::with_name("aws_secret_access_key")
                .short("s")
                .long("aws_secret_access_key")
                .takes_value(true)
                .help("AWS secret access key"),
        )
        .subcommand(SubCommand::with_name("s3").about("s3 test"))
        .subcommand(SubCommand::with_name("s3_rusoto").about("s3 rusoto test"))
        .subcommand(SubCommand::with_name("create").about("create test "))
        .subcommand(SubCommand::with_name("write").about("write test"))
        .subcommand(SubCommand::with_name("read").about("read test"))
        .subcommand(SubCommand::with_name("free").about("free test"))
        .subcommand(SubCommand::with_name("btree").about("btree test"))
        .subcommand(SubCommand::with_name("nvpair").about("nvpair test"))
        .subcommand(SubCommand::with_name("rusoto_role").about("rusoto credentials test"))
        .subcommand(SubCommand::with_name("list_pools").about("list pools"))
        .subcommand(SubCommand::with_name("list_pool_objects").about("list pool objects"))
        .subcommand(
            SubCommand::with_name("destroy_old_pools")
                .about("destroy old pools")
                .arg(
                    Arg::with_name("number-of-days")
                        .short("d")
                        .long("number-of-days")
                        .required(true)
                        .takes_value(true)
                        .help("number of days"),
                ),
        )
        .subcommand(SubCommand::with_name("test_connectivity").about("test connectivity"))
        .get_matches();

    // Command line parameters
    let endpoint = matches.value_of("endpoint").unwrap();
    let region_str = matches.value_of("region").unwrap();
    let bucket_name = matches.value_of("bucket").unwrap();
    let profile = matches.value_of("profile").unwrap();
    let aws_access_key_id = matches.value_of("aws_access_key_id");
    let aws_secret_access_key = matches.value_of("aws_secret_access_key");

    if aws_access_key_id.is_some() != aws_secret_access_key.is_some() {
        matches.usage();
        panic!("Error: Both aws_access_key_id and aws_secret_access_key should be specified.");
    }

    println!(
        "endpoint: {}, region: {}, bucket: {} profile: {} access_id: {:?}, secret_key: {:?}",
        endpoint, region_str, bucket_name, profile, aws_access_key_id, aws_secret_access_key
    );

    let object_access: ObjectAccess = get_object_access(
        endpoint,
        region_str,
        bucket_name,
        profile,
        aws_access_key_id,
        aws_secret_access_key,
    );

    match matches.subcommand() {
        ("s3_rusoto", Some(_matches)) => {
            do_s3_rusoto().await.unwrap();
        }
        ("create", Some(_matches)) => {
            do_create().await.unwrap();
        }
        ("write", Some(_matches)) => {
            do_write().await.unwrap();
        }
        ("read", Some(_matches)) => {
            do_read().await.unwrap();
        }
        ("free", Some(_matches)) => {
            do_free().await.unwrap();
        }
        ("btree", Some(_matches)) => {
            do_btree();
        }
        ("nvpair", Some(_matches)) => {
            do_nvpair();
        }
        ("rusoto_role", Some(_matches)) => {
            do_rusoto_role().await.unwrap();
        }
        ("list_pools", Some(_matches)) => {
            do_list_pools(&object_access, false).await.unwrap();
        }
        ("list_pool_objects", Some(_matches)) => {
            do_list_pools(&object_access, true).await.unwrap();
        }
        ("destroy_old_pools", Some(destroy_matches)) => {
            let age_str = destroy_matches.value_of("number-of-days").unwrap();
            let min_age = Duration::from_secs(age_str.parse::<u64>().unwrap() * 60 * 60 * 24);

            do_destroy_old_pools(&object_access, min_age).await.unwrap();
        }
        ("test_connectivity", Some(_matches)) => {
            test_connectivity(&object_access).await.unwrap();
        }
        _ => {
            matches.usage();
        }
    };
}

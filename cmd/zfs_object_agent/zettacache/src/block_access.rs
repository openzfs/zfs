use crate::base_types::DiskLocation;
use crate::base_types::Extent;
use anyhow::{anyhow, Result};
use bincode::Options;
use lazy_static::lazy_static;
use log::*;
use metered::common::*;
use metered::hdr_histogram::AtomicHdrHistogram;
use metered::metered;
use metered::time_source::StdInstantMicros;
use more_asserts::*;
use nix::sys::stat::SFlag;
use num::Num;
use num::NumCast;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use std::convert::TryFrom;
use std::io::Read;
use std::io::Write;
use std::os::unix::prelude::AsRawFd;
use std::sync::Arc;
use std::time::Instant;
use tokio::fs::File;
use tokio::fs::OpenOptions;
use tokio::sync::OwnedSemaphorePermit;
use tokio::sync::Semaphore;
use tokio::task::JoinHandle;
use util::get_tunable;
use util::From64;

lazy_static! {
    static ref MIN_SECTOR_SIZE: usize = get_tunable("min_sector_size", 512);
    static ref DISK_WRITE_MAX_QUEUE_DEPTH: usize = get_tunable("disk_write_max_queue_depth", 32);
    static ref DISK_WRITE_METADATA_MAX_QUEUE_DEPTH: usize =
        get_tunable("disk_write_metadata_max_queue_depth", 32);
    static ref DISK_READ_MAX_QUEUE_DEPTH: usize = get_tunable("disk_read_max_queue_depth", 64);
}

#[derive(Serialize, Deserialize, Debug)]
struct BlockHeader {
    payload_size: usize,
    encoding: EncodeType,
    compression: CompressType,
    checksum: u64,
}

#[derive(Debug)]
pub struct BlockAccess {
    disk: File,
    size: u64,
    sector_size: usize,
    metrics: BlockAccessMetrics,
    outstanding_reads: Semaphore,
    outstanding_data_writes: Arc<Semaphore>,
    outstanding_metadata_writes: Arc<Semaphore>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum CompressType {
    None,
    Lz4,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum EncodeType {
    Json,
    Bincode,
}

pub struct WritePermit(OwnedSemaphorePermit);

// Generate ioctl function
nix::ioctl_read!(ioctl_blkgetsize64, 0x12u8, 114u8, u64);
nix::ioctl_read_bad!(ioctl_blksszget, 0x1268, usize);

#[cfg(target_os = "linux")]
const CUSTOM_OFLAGS: i32 = libc::O_DIRECT;
#[cfg(not(target_os = "linux"))]
const CUSTOM_OFLAGS: i32 = 0;

// XXX this is very thread intensive.  On Linux, we can use "glommio" to use
// io_uring for much lower overheads.  Or SPDK (which can use io_uring or nvme
// hardware directly).  Or at least use O_DIRECT.
#[metered(registry=BlockAccessMetrics)]
impl BlockAccess {
    pub async fn new(disk_path: &str) -> BlockAccess {
        let disk = OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(CUSTOM_OFLAGS)
            .open(disk_path)
            .await
            .unwrap();
        let stat = nix::sys::stat::fstat(disk.as_raw_fd()).unwrap();
        trace!("stat: {:?}", stat);
        let mode = SFlag::from_bits_truncate(stat.st_mode);
        let sector_size;
        let size;
        if mode.contains(SFlag::S_IFBLK) {
            size = unsafe {
                let mut cap: u64 = 0;
                let cap_ptr = &mut cap as *mut u64;
                ioctl_blkgetsize64(disk.as_raw_fd(), cap_ptr).unwrap();
                cap
            };
            sector_size = unsafe {
                let mut ssz: usize = 0;
                let ssz_ptr = &mut ssz as *mut usize;
                ioctl_blksszget(disk.as_raw_fd(), ssz_ptr).unwrap();
                ssz
            };
        } else if mode.contains(SFlag::S_IFREG) {
            size = u64::try_from(stat.st_size).unwrap();
            sector_size = *MIN_SECTOR_SIZE;
        } else {
            panic!("{}: invalid file type {:?}", disk_path, mode);
        }

        let this = BlockAccess {
            disk,
            size,
            sector_size,
            metrics: Default::default(),
            outstanding_reads: Semaphore::new(*DISK_READ_MAX_QUEUE_DEPTH),
            outstanding_data_writes: Arc::new(Semaphore::new(*DISK_WRITE_MAX_QUEUE_DEPTH)),
            outstanding_metadata_writes: Arc::new(Semaphore::new(
                *DISK_WRITE_METADATA_MAX_QUEUE_DEPTH,
            )),
        };
        info!("opening cache file {}: {:?}", disk_path, this);

        this
    }

    pub fn size(&self) -> u64 {
        self.size
    }

    pub fn dump_metrics(&self) {
        debug!("metrics: {:#?}", self.metrics);
    }

    // offset and length must be sector-aligned
    // maybe this should return Bytes?
    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    pub async fn read_raw(&self, extent: Extent) -> Vec<u8> {
        assert_eq!(extent.size, self.round_up_to_sector(extent.size));
        let fd = self.disk.as_raw_fd();
        let sector_size = self.sector_size;
        let begin = Instant::now();
        let _permit = self.outstanding_reads.acquire().await.unwrap();
        let vec = tokio::task::spawn_blocking(move || {
            let mut v: Vec<u8> = Vec::new();
            // XXX use unsafe code to avoid double initializing it?
            // XXX directio requires the pointer to be sector-aligned, requiring this grossness
            v.resize(usize::from64(extent.size) + sector_size, 0);
            let aligned = unsafe {
                let ptr = v.as_mut_ptr() as usize;
                let aligned_ptr = (ptr + sector_size - 1) / sector_size * sector_size;
                assert_le!(aligned_ptr - v.as_mut_ptr() as usize, sector_size);
                std::slice::from_raw_parts_mut(aligned_ptr as *mut u8, usize::from64(extent.size))
            };
            nix::sys::uio::pread(fd, aligned, i64::try_from(extent.location.offset).unwrap())
                .unwrap();
            // XXX copying again!
            aligned.to_owned()
        })
        .await
        .unwrap();
        trace!(
            "read({:?}) returned in {}us",
            extent,
            begin.elapsed().as_micros()
        );
        vec
    }

    // Acquire a permit to write later.  This should be used only for data
    // writes.  See the comment in write_raw() for details.
    pub async fn acquire_write(&self) -> WritePermit {
        WritePermit(
            self.outstanding_data_writes
                .clone()
                .acquire_owned()
                .await
                .unwrap(),
        )
    }

    // offset and data.len() must be sector-aligned
    // maybe this should take Bytes?
    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    pub async fn write_raw(&self, location: DiskLocation, data: Vec<u8>) {
        // We need a different semaphore for metadata writes, so that
        // outstanding data write permits can't starve/deadlock metadata writes.
        // We may block on locks (e.g. waiting on the ZettaCacheState lock)
        // while holding a data write permit, but we can't while holding a
        // metadata write permit.
        let permit = WritePermit(
            self.outstanding_metadata_writes
                .clone()
                .acquire_owned()
                .await
                .unwrap(),
        );
        self.write_raw_permit(permit, location, data).await.unwrap();
    }

    // offset and data.len() must be sector-aligned
    // maybe this should take Bytes?
    #[measure(type = ResponseTime<AtomicHdrHistogram, StdInstantMicros>)]
    #[measure(InFlight)]
    #[measure(Throughput)]
    #[measure(HitCount)]
    pub fn write_raw_permit(
        &self,
        permit: WritePermit,
        location: DiskLocation,
        data: Vec<u8>,
    ) -> JoinHandle<()> {
        let fd = self.disk.as_raw_fd();
        let sector_size = self.sector_size;
        let length = data.len();
        let offset = location.offset;
        assert_eq!(offset, self.round_up_to_sector(offset));
        assert_eq!(length, self.round_up_to_sector(length));
        let begin = Instant::now();
        tokio::task::spawn_blocking(move || {
            let mut v: Vec<u8> = Vec::new();
            // XXX directio requires the pointer to be sector-aligned, requiring this grossness
            v.resize(length + sector_size, 0);
            let aligned = unsafe {
                let ptr = v.as_mut_ptr() as usize;
                let aligned_ptr = (ptr + sector_size - 1) / sector_size * sector_size;
                assert_le!(aligned_ptr - v.as_mut_ptr() as usize, sector_size);
                std::slice::from_raw_parts_mut(aligned_ptr as *mut u8, length)
            };
            // XXX copying
            aligned.copy_from_slice(&data);
            nix::sys::uio::pwrite(fd, aligned, i64::try_from(offset).unwrap()).unwrap();
            drop(permit);
            trace!(
                "write({:?} len={}) returned in {}us",
                location,
                length,
                begin.elapsed().as_micros()
            );
        })
    }

    pub fn round_up_to_sector<N: Num + NumCast + Copy>(&self, n: N) -> N {
        let sector_size: N = NumCast::from(self.sector_size).unwrap();
        (n + sector_size - N::one()) / sector_size * sector_size
    }

    // XXX ideally this would return a sector-aligned address, so it can be used directly for a directio write
    pub fn chunk_to_raw<T: Serialize>(&self, encoding: EncodeType, struct_obj: &T) -> Vec<u8> {
        let (payload, compression) = match encoding {
            EncodeType::Json => {
                let json = serde_json::to_vec(struct_obj).unwrap();
                let mut lz4_encoder = lz4::EncoderBuilder::new()
                    .level(4)
                    .build(Vec::new())
                    .unwrap();
                lz4_encoder.write_all(&json).unwrap();
                let (payload, result) = lz4_encoder.finish();
                result.unwrap();
                (payload, CompressType::Lz4)
            }
            EncodeType::Bincode => {
                let payload = Self::bincode_options().serialize(struct_obj).unwrap();
                // XXX It's faster to not lz4 compress this, even though
                // compression would get us around 2x (27B -> 14B for index
                // entries).  But if we were to use multiple CPU's, or be able
                // to do this incrementally in the background so that we don't
                // need the absolute maximum throughput, then we should try
                // compression again.  The decompression time would still be
                // relevant but is much faster than compression so might be
                // fine.
                (payload, CompressType::None)
            }
        };

        let header = BlockHeader {
            payload_size: payload.len(),
            encoding,
            compression,
            checksum: seahash::hash(&payload),
        };
        let header_bytes = serde_json::to_vec(&header).unwrap();

        let mut buf = Vec::new();
        buf.extend_from_slice(&header_bytes);
        // Encode a NUL byte after the header, so that we know where it ends.
        buf.extend_from_slice(&[0]);
        // XXX copying data around; use bincode::serialize_into() to append it into a larger-than-necessary vec?
        buf.extend_from_slice(&payload);
        buf.resize(self.round_up_to_sector(buf.len()), 0);

        buf
    }

    fn bincode_options() -> impl bincode::Options {
        // Note: DefaultOptions uses varint encoding (unlike bincode::serialize())
        bincode::DefaultOptions::new()
    }

    /// returns deserialized struct and amount of the buf that was consumed
    pub fn chunk_from_raw<T: DeserializeOwned>(&self, buf: &[u8]) -> Result<(T, usize)> {
        // size includes the terminating NUL byte
        let header_size = buf.iter().position(|&c| c == b'\0').unwrap() + 1;
        let header: BlockHeader = serde_json::from_slice(&buf[..header_size - 1])?;

        if header.payload_size > buf.len() - header_size {
            return Err(anyhow!(
                "invalid length {}: expected at most {} bytes",
                header.payload_size,
                buf.len() - header_size
            ));
        }

        let data = &buf[header_size..header.payload_size + header_size];
        assert_eq!(data.len(), header.payload_size);
        let actual_checksum = seahash::hash(data);
        if header.checksum != actual_checksum {
            return Err(anyhow!(
                "incorrect checksum of {} bytes: expected {:x}, got {:x}",
                header.payload_size,
                header.checksum,
                actual_checksum,
            ));
        }

        let mut serde_vec = Vec::new();
        let serde_slice = match header.compression {
            CompressType::None => data,
            CompressType::Lz4 => {
                let mut decoder = lz4::Decoder::new(data).unwrap();
                decoder.read_to_end(&mut serde_vec).unwrap();
                let (_, result) = decoder.finish();
                result.unwrap();
                &serde_vec
            }
        };

        let struct_obj: T = match header.encoding {
            EncodeType::Json => serde_json::from_slice(serde_slice)?,
            EncodeType::Bincode => Self::bincode_options().deserialize(serde_slice)?,
        };
        Ok((
            struct_obj,
            self.round_up_to_sector(header_size + data.len()),
        ))
    }
}

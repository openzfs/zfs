use more_asserts::*;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use std::fmt::*;
use std::ops::Add;
use std::ops::Sub;
use util::From64;

/*
 * Things that are stored on disk.
 */
pub trait OnDisk: Serialize + DeserializeOwned {}

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd, Hash)]
pub struct PoolGuid(pub u64);
impl OnDisk for PoolGuid {}
impl Display for PoolGuid {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{:020}", self.0)
    }
}

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd, Hash)]
pub struct BlockId(pub u64);
impl OnDisk for BlockId {}
impl Display for BlockId {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{}", self.0)
    }
}
impl BlockId {
    pub fn next(&self) -> BlockId {
        BlockId(self.0 + 1)
    }
}

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct DiskLocation {
    // note: will need to add disk ID to support multiple disks
    pub offset: u64,
}
impl Add<u64> for DiskLocation {
    type Output = DiskLocation;
    fn add(self, rhs: u64) -> Self::Output {
        DiskLocation {
            offset: self.offset + rhs,
        }
    }
}
impl Add<usize> for DiskLocation {
    type Output = DiskLocation;
    fn add(self, rhs: usize) -> Self::Output {
        self + rhs as u64
    }
}

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct Extent {
    pub location: DiskLocation,
    pub size: u64,
}

impl Extent {
    pub fn range(&self, relative_offset: u64, size: u64) -> Extent {
        assert_ge!(self.size, relative_offset + size);
        Extent {
            location: self.location + relative_offset,
            size,
        }
    }
}

#[derive(Serialize, Deserialize, Default, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct CheckpointId(pub u64);
impl CheckpointId {
    pub fn next(&self) -> CheckpointId {
        CheckpointId(self.0 + 1)
    }
}

#[derive(Serialize, Deserialize, Default, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct Atime(pub u64);
impl Atime {
    pub fn next(&self) -> Atime {
        Atime(self.0 + 1)
    }
}

impl Sub<Atime> for Atime {
    type Output = usize;
    fn sub(self, rhs: Atime) -> usize {
        usize::from64(self.0 - rhs.0)
    }
}

#[derive(Serialize, Deserialize, Default, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct ReclaimLogId(pub u16);
impl Display for ReclaimLogId {
    // For crash cleanup we want value prefixed with zeroes and there can be up to 2 ^ 16 logs
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{:05}", self.0)
    }
}

impl ReclaimLogId {
    pub fn as_index(self) -> usize {
        usize::from(self.0)
    }
}

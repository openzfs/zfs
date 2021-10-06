use serde::{Deserialize, Serialize};
use std::fmt::*;
use zettacache::base_types::OnDisk;

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd)]
pub struct Txg(pub u64);
impl OnDisk for Txg {}
impl Display for Txg {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{:020}", self.0)
    }
}

impl Txg {
    pub fn checked_sub(self, rhs: u64) -> Option<Txg> {
        if self.0 < rhs {
            None
        } else {
            Some(Txg(self.0 - rhs))
        }
    }
}

#[derive(Serialize, Deserialize, Debug, Copy, Clone, PartialEq, Eq, Ord, PartialOrd, Hash)]
pub struct ObjectId(pub u64);
impl OnDisk for ObjectId {}
impl Display for ObjectId {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "{:020}", self.0)
    }
}
impl ObjectId {
    pub fn next(&self) -> ObjectId {
        ObjectId(self.0 + 1)
    }
}

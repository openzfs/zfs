use serde::Deserialize;
use serde::Serialize;
use std::fmt::Debug;
use std::fmt::Formatter;
use std::fmt::Result;

/// exists just to reduce Debug output on fields we don't really care about
#[derive(Serialize, Deserialize, Clone)]
pub struct TerseVec<T>(pub Vec<T>);

impl<T> Debug for TerseVec<T> {
    fn fmt(&self, fmt: &mut Formatter) -> Result {
        fmt.write_fmt(format_args!("[...{} elements...]", self.0.len()))
    }
}

impl<T> From<Vec<T>> for TerseVec<T> {
    fn from(vec: Vec<T>) -> Self {
        Self(vec)
    }
}

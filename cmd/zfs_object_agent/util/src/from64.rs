use std::convert::TryInto;

/// Conversions that are safe assuming that we are on LP64 (usize == u64)
pub trait From64<A> {
    fn from64(a: A) -> Self;
}

impl From64<u64> for usize {
    fn from64(a: u64) -> usize {
        a.try_into().unwrap()
    }
}

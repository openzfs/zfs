use roaring::bitmap::Iter;
use roaring::RoaringBitmap;
use std::iter::Fuse;

/// An iterator over the ranges of set bits on a bitmap
///
/// This `struct` is created by the [`iter_ranges`] method on
/// [`BitmapRangeIterator`]. See its documentation for more.
///
/// [`iter_ranges`]: BitmapRangeIterator::iter_ranges
pub struct BitmapRangeIter<'a> {
    bitmap_iter: Fuse<Iter<'a>>,
    current_range: Option<(u32, u32)>,
}

impl<'a> BitmapRangeIter<'a> {
    fn new(bitmap: &RoaringBitmap) -> BitmapRangeIter {
        BitmapRangeIter {
            bitmap_iter: bitmap.iter().fuse(),
            current_range: None,
        }
    }
}

impl<'a> Iterator for BitmapRangeIter<'a> {
    // Starting and ending indices representing a range on a bitmap.  Note that
    // the limits of the range are inclusive! (e.g. [start, end] as opposed to
    // [start, end)).
    type Item = (u32, u32);

    fn next(&mut self) -> Option<(u32, u32)> {
        loop {
            match self.bitmap_iter.next() {
                Some(slot) => match self.current_range {
                    Some((first, last)) if slot == (last + 1) => {
                        self.current_range = Some((first, slot));
                    }
                    Some((first, last)) => {
                        self.current_range = Some((slot, slot));
                        return Some((first, last));
                    }
                    None => {
                        self.current_range = Some((slot, slot));
                    }
                },
                None => return self.current_range.take(),
            }
        }
    }
}

/// An interface that provides iterators operating on ranges of a bitmap.
///
/// Currently the only implementation of this interface is Roaring Bitmaps
/// (<https://docs.rs/roaring/0.7.0/roaring/bitmap/struct.RoaringBitmap.html>).
pub trait BitmapRangeIterator {
    /// Gets an iterator over the ranges of set bits on a bitmap.
    ///
    /// NOTE: The ranges returned are inclusive ranges (e.g. [start, end])
    /// where the start and end indeces are bundled in a tuple.
    fn iter_ranges(&self) -> BitmapRangeIter;
}

impl BitmapRangeIterator for RoaringBitmap {
    fn iter_ranges(&self) -> BitmapRangeIter {
        BitmapRangeIter::new(self)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use more_asserts::assert_ge;

    fn validate_iter_ranges(a: &RoaringBitmap) {
        let mut total_segments = 0;
        for (first, last) in a.iter_ranges() {
            assert_ge!(last, first);
            for slot in first..last + 1 {
                assert!(a.contains(slot));
            }
            total_segments += last - first + 1;
        }
        assert_eq!(u64::from(total_segments), a.len());
    }

    #[test]
    fn test_empty() {
        let a = RoaringBitmap::new();
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_single_start_slot() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_single_start_range() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..2);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_single_start_range_2() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..3);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_single_middle_slot() {
        let mut a = RoaringBitmap::new();
        a.insert(5);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_single_middle_range() {
        let mut a = RoaringBitmap::new();
        a.insert_range(5..7);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_single_middle_range_2() {
        let mut a = RoaringBitmap::new();
        a.insert_range(5..8);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_two_slots_start_end() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_slot_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_range_end_slot() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..2);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_range_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..2);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_two_slots_middle_end() {
        let mut a = RoaringBitmap::new();
        a.insert(5);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_middle_slot_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert(5);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_middle_range_end_slot() {
        let mut a = RoaringBitmap::new();
        a.insert_range(5..7);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_middle_range_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert_range(5..7);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_three_slots_start_middle_end() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        a.insert(5);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_range_middle_slot_end_slot() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..2);
        a.insert(5);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_slot_middle_range_end_slot() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        a.insert_range(5..7);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_slot_middle_slot_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        a.insert(5);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_slot_middle_range_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert(0);
        a.insert_range(5..7);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_range_middle_range_end_slot() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..2);
        a.insert_range(5..7);
        a.insert(10);
        validate_iter_ranges(&a);
    }

    #[test]
    fn test_start_range_middle_range_end_range() {
        let mut a = RoaringBitmap::new();
        a.insert_range(0..2);
        a.insert_range(5..7);
        a.insert_range(10..12);
        validate_iter_ranges(&a);
    }
}

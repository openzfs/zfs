use more_asserts::*;
use std::{collections::BTreeMap, ops::RangeBounds};

#[derive(Default)]
pub struct RangeTree {
    tree: BTreeMap<u64, u64>, // start -> size
    space: u64,
}

impl RangeTree {
    pub fn new() -> RangeTree {
        RangeTree {
            tree: BTreeMap::new(),
            space: 0,
        }
    }

    // panics if already present
    pub fn add(&mut self, start: u64, size: u64) {
        if size == 0 {
            return;
        }

        let end = start + size;
        let before = self.tree.range(..end).next_back();
        let after = self.tree.range(start..).next();

        let merge_before = match before {
            Some((&before_start, &before_size)) => {
                assert_le!(before_start + before_size, start);
                before_start + before_size == start
            }
            None => false,
        };

        let merge_after = match after {
            Some((&after_start, &_after_size)) => {
                assert_ge!(after_start, end);
                after_start == end
            }
            None => false,
        };

        if merge_before && merge_after {
            let &before_start = before.unwrap().0;
            let (&after_start, &after_size) = after.unwrap();
            self.tree
                .entry(before_start)
                .and_modify(|before_size| *before_size += size + after_size);
            self.tree.remove(&after_start);
        } else if merge_before {
            let before_start = *before.unwrap().0;
            self.tree
                .entry(before_start)
                .and_modify(|before_size| *before_size += size);
        } else if merge_after {
            let after_start = *after.unwrap().0;
            let after_size = *after.unwrap().1;
            self.tree.remove(&after_start);
            self.tree.insert(start, size + after_size);
        } else {
            self.tree.insert(start, size);
        }
        self.space += size;
    }

    // panics if not present
    pub fn remove(&mut self, start: u64, size: u64) {
        assert_ne!(size, 0);

        let end = start + size;
        let (&existing_start, existing_size_ref) = self.tree.range_mut(..end).next_back().unwrap();
        let existing_end = existing_start + *existing_size_ref;
        assert_le!(existing_start, start);
        assert_ge!(existing_end, end);
        let left_over = existing_start != start;
        let right_over = existing_end != end;

        if left_over && right_over {
            *existing_size_ref = start - existing_start;
            self.tree.insert(end, existing_end - end);
        } else if left_over {
            *existing_size_ref = start - existing_start;
        } else if right_over {
            self.tree.remove(&start);
            self.tree.insert(end, existing_end - end);
        } else {
            self.tree.remove(&start);
        }
        self.space -= size;
    }

    pub fn verify_absent(&self, start: u64, size: u64) {
        assert_ne!(size, 0);

        let end = start + size;
        if let Some((&existing_start, &existing_size)) = self.tree.range(..end).next_back() {
            let existing_end = existing_start + existing_size;
            if existing_start <= start && existing_end >= end {
                panic!(
                    "range_tree segment [{}, {}) is not absent (overlaps with segment [{}, {}))",
                    start, end, existing_start, existing_end
                );
            }
        }
    }

    /// Returns Iter<start, size>
    pub fn iter(&self) -> std::collections::btree_map::Iter<u64, u64> {
        self.tree.iter()
    }

    pub fn range<R>(&self, range: R) -> std::collections::btree_map::Range<'_, u64, u64>
    where
        R: RangeBounds<u64>,
    {
        self.tree.range(range)
    }

    pub fn clear(&mut self) {
        self.tree.clear();
        self.space = 0;
    }

    pub fn space(&self) -> u64 {
        self.space
    }

    pub fn verify_space(&self) {
        assert_eq!(self.space, self.tree.values().sum::<u64>())
    }
}

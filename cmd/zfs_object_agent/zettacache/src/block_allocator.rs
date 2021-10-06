use crate::base_types::*;
use crate::block_access::BlockAccess;
use crate::extent_allocator::ExtentAllocator;
use crate::space_map::{SpaceMap, SpaceMapEntry, SpaceMapPhys};
use crate::zettacache::DEFAULT_SLAB_SIZE;
use lazy_static::lazy_static;
use log::{debug, trace};
use more_asserts::*;
use num::range;
use roaring::RoaringBitmap;
use serde::{Deserialize, Serialize};
use std::cmp::min;
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::convert::TryFrom;
use std::ops::Bound::*;
use std::sync::Arc;
use std::time::Instant;
use std::{iter, mem};
use util::BitmapRangeIterator;
use util::From64;
use util::RangeTree;
use util::{get_tunable, TerseVec};

lazy_static! {
    static ref DEFAULT_SLAB_BUCKETS: SlabAllocationBucketsPhys =
        get_tunable("default_slab_buckets", SlabAllocationBucketsPhys::default());
    static ref SLAB_CONDENSE_PER_CHECKPOINT: u64 = get_tunable("slab_condense_per_checkpoint", 10);
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
pub struct SlabId(u64);

impl SlabId {
    fn as_index(&self) -> usize {
        usize::from64(self.0)
    }

    pub fn next(&self) -> SlabId {
        SlabId(self.0 + 1)
    }
}

trait SlabTrait {
    fn import_alloc(&mut self, extent: Extent);
    fn import_free(&mut self, extent: Extent);
    fn allocate(&mut self, size: u32) -> Option<Extent>;
    fn free(&mut self, extent: Extent);
    fn flush_to_spacemap(&mut self, spacemap: &mut SpaceMap);
    fn condense_to_spacemap(&self, spacemap: &mut SpaceMap);
    fn get_max_size(&self) -> u32;
    fn get_free_space(&self) -> u64;
    fn get_allocated_space(&self) -> u64;
    fn get_phys_type(&self) -> SlabPhysType;
}

struct BitmapSlab {
    allocatable: RoaringBitmap,
    allocating: RoaringBitmap,
    freeing: RoaringBitmap,

    total_slots: u32,
    slot_size: u32,
    slab_offset: u64,
}

impl BitmapSlab {
    fn new_slab(
        id: SlabId,
        generation: u64,
        slab_offset: u64,
        slab_size: u32,
        block_size: u32,
    ) -> Slab {
        let free_slots = slab_size / block_size;
        let mut allocatable = RoaringBitmap::new();

        allocatable.insert_range(0..free_slots.into());
        assert_eq!(allocatable.len(), u64::from(free_slots));

        Slab::new(
            id,
            generation,
            SlabType::BitmapBased(BitmapSlab {
                allocatable,
                allocating: Default::default(),
                freeing: Default::default(),
                total_slots: free_slots,
                slot_size: block_size,
                slab_offset,
            }),
        )
    }

    fn to_offset(&self, slot: u32) -> u64 {
        self.slab_offset + u64::from(slot * self.slot_size)
    }

    fn end_offset(&self) -> u64 {
        self.to_offset(self.total_slots)
    }

    fn verify_slab_extent(&self, extent: Extent) {
        assert_eq!(extent.size % u64::from(self.get_max_size()), 0);
        assert_ge!(extent.location.offset, self.slab_offset);
        assert_le!(extent.location.offset + extent.size, self.end_offset());
    }

    fn import_extent_impl(&mut self, extent: Extent, is_alloc: bool) {
        self.verify_slab_extent(extent);

        let internal_offset = u32::try_from(extent.location.offset - self.slab_offset).unwrap();
        assert_eq!(internal_offset % self.slot_size, 0);
        let num_slots = u32::try_from(extent.size).unwrap() / self.slot_size;
        assert_ge!(num_slots, 1);

        let first_slot = internal_offset / self.slot_size;
        assert_le!(first_slot + num_slots, self.total_slots);
        let slot_range = first_slot.into()..u64::from(first_slot + num_slots);
        if is_alloc {
            let removed = self.allocatable.remove_range(slot_range);
            assert_eq!(
                removed,
                u64::from(num_slots),
                "double alloc detected during import"
            );
        } else {
            let inserted = self.allocatable.insert_range(slot_range);
            assert_eq!(
                inserted,
                u64::from(num_slots),
                "double free detected during import"
            );
            assert_lt!(
                self.allocatable.max().unwrap(),
                self.total_slots,
                "FREE segment crosses the slab's end boundary"
            )
        }
    }
}

impl SlabTrait for BitmapSlab {
    fn import_alloc(&mut self, extent: Extent) {
        self.import_extent_impl(extent, true);
    }

    fn import_free(&mut self, extent: Extent) {
        self.import_extent_impl(extent, false);
    }

    fn allocate(&mut self, size: u32) -> Option<Extent> {
        assert_ge!(self.slot_size, size);
        if self.allocatable.is_empty() {
            return None;
        }

        let slot = self.allocatable.min().unwrap();
        let inserted = self.allocating.insert(slot);
        assert!(inserted);
        self.allocatable.remove(slot);

        // Cannot be allocating a block that's currently in the
        // middle of being freed.
        assert!(!self.freeing.contains(slot));

        Some(Extent {
            location: DiskLocation {
                offset: self.to_offset(slot),
            },
            size: self.slot_size.into(),
        })
    }

    fn free(&mut self, extent: Extent) {
        self.verify_slab_extent(extent);

        let internal_offset = u32::try_from(extent.location.offset - self.slab_offset).unwrap();
        assert_eq!(internal_offset % self.slot_size, 0);

        let slot = internal_offset / self.slot_size;
        assert!(
            !self.allocatable.contains(slot),
            "double free at slot {:?}",
            slot
        );

        let inserted = self.freeing.insert(slot);
        assert!(inserted);
    }

    fn flush_to_spacemap(&mut self, spacemap: &mut SpaceMap) {
        // It could happen that a segment was allocated and then freed within
        // the same checkpoint period at which point it would be part of both
        // `allocating` and `freeing` sets. For this reason we always record
        // `allocating` first, before `freeing`, on our spacemaps. Note that
        // segments cannot be freed and then allocated within the same
        // checkpoint period.
        for (first, last) in self.allocating.iter_ranges() {
            assert_ge!(last, first);
            spacemap.alloc(
                self.to_offset(first),
                u64::from((last - first + 1) * self.slot_size),
            );
        }
        self.allocating.clear();

        for (first, last) in self.freeing.iter_ranges() {
            assert_ge!(last, first);
            spacemap.free(
                self.to_offset(first),
                u64::from((last - first + 1) * self.slot_size),
            );
        }
        // Space freed during this checkpoint is now available for reallocation.
        for slot in self.freeing.iter() {
            let inserted = self.allocatable.insert(slot);
            assert!(inserted);
        }
        self.freeing.clear();
    }

    fn condense_to_spacemap(&self, spacemap: &mut SpaceMap) {
        // TODO: In the future we may want to check if writing the whole
        //       RoaringBitmap as a first-class spacemap entry is more
        //       practical here.
        let mut written_segments = 0;
        let mut alloc_offset = self.to_offset(0);
        for slot in self.allocatable.iter() {
            let slot_offset = self.to_offset(slot);
            spacemap.alloc(alloc_offset, slot_offset - alloc_offset);
            written_segments += (slot_offset - alloc_offset) / u64::from(self.slot_size);
            alloc_offset = slot_offset + u64::from(self.slot_size);
        }
        spacemap.alloc(alloc_offset, self.end_offset() - alloc_offset);
        written_segments += (self.end_offset() - alloc_offset) / u64::from(self.slot_size);
        assert_eq!(
            written_segments,
            u64::from(self.total_slots) - self.allocatable.len()
        );

        // In our attempt to make this independent of flush_to_spacemap(), we do
        // not mutate any of the in-memory data structures and mark all entries
        // from the allocating bitmap as free. The latter is because these
        // entries will be later marked as allocated in flush_to_spacemap().
        for (first, last) in self.allocating.iter_ranges() {
            spacemap.free(
                self.to_offset(first),
                u64::from((last - first + 1) * self.slot_size),
            );
        }
    }

    fn get_max_size(&self) -> u32 {
        self.slot_size
    }

    fn get_free_space(&self) -> u64 {
        self.allocatable.len() * u64::from(self.slot_size)
    }

    fn get_allocated_space(&self) -> u64 {
        (u64::from(self.total_slots) - self.allocatable.len()) * u64::from(self.slot_size)
    }

    fn get_phys_type(&self) -> SlabPhysType {
        SlabPhysType::BitmapBased {
            block_size: self.slot_size,
        }
    }
}

struct ExtentSlab {
    allocatable: RangeTree,
    allocating: RangeTree,
    freeing: RangeTree,
    last_location: u64,

    total_space: u64,
    max_allowed_alloc_size: u32,
    slab_offset: u64,
}

impl ExtentSlab {
    fn new_slab(
        id: SlabId,
        generation: u64,
        slab_offset: u64,
        slab_size: u32,
        max_allowed_alloc_size: u32,
    ) -> Slab {
        let mut allocatable: RangeTree = Default::default();
        allocatable.add(slab_offset, slab_size.into());
        Slab::new(
            id,
            generation,
            SlabType::ExtentBased(ExtentSlab {
                allocatable,
                allocating: Default::default(),
                freeing: Default::default(),
                last_location: 0,
                total_space: slab_size.into(),
                max_allowed_alloc_size,
                slab_offset,
            }),
        )
    }

    fn verify_slab_extent(&self, extent: Extent) {
        assert_ge!(extent.location.offset, self.slab_offset);
        assert_le!(
            extent.location.offset + extent.size,
            self.slab_offset + self.total_space
        );
    }

    fn allocate_impl(&mut self, size: u64, min_location: u64, max_location: u64) -> Option<Extent> {
        for (&allocatable_offset, &allocatable_size) in
            self.allocatable.range(min_location..max_location)
        {
            if allocatable_size >= size {
                self.freeing.verify_absent(allocatable_offset, size);
                self.allocatable.remove(allocatable_offset, size);
                self.allocating.add(allocatable_offset, size);
                self.last_location = allocatable_offset + size;
                return Some(Extent {
                    location: DiskLocation {
                        offset: allocatable_offset,
                    },
                    size,
                });
            }
        }
        None
    }
}

impl SlabTrait for ExtentSlab {
    fn import_alloc(&mut self, extent: Extent) {
        self.verify_slab_extent(extent);
        self.allocatable.remove(extent.location.offset, extent.size);
    }

    fn import_free(&mut self, extent: Extent) {
        self.verify_slab_extent(extent);
        self.allocatable.add(extent.location.offset, extent.size);
    }

    fn allocate(&mut self, size: u32) -> Option<Extent> {
        assert_le!(size, self.get_max_size());
        let request_size = u64::from(size);
        // find next segment where this fits
        match self.allocate_impl(request_size, self.last_location, u64::MAX) {
            Some(e) => Some(e),
            None => self.allocate_impl(request_size, 0, self.last_location),
        }
    }

    fn free(&mut self, extent: Extent) {
        self.verify_slab_extent(extent);
        self.allocatable
            .verify_absent(extent.location.offset, extent.size);
        self.allocating
            .verify_absent(extent.location.offset, extent.size);
        self.freeing.add(extent.location.offset, extent.size);
    }

    fn flush_to_spacemap(&mut self, spacemap: &mut SpaceMap) {
        self.freeing.verify_space();
        self.allocating.verify_space();
        self.allocatable.verify_space();

        // It could happen that a segment was allocated and then freed within
        // the same checkpoint period at which point it would be part of both
        // `allocating` and `freeing` sets. For this reason we always record
        // `allocating` first, before `freeing`, on our spacemaps. Note that
        // segments cannot be freed and then allocated within the same
        // checkpoint period.
        for (&start, &size) in self.allocating.iter() {
            self.allocatable.verify_absent(start, size);
            spacemap.alloc(start, size);
        }
        self.allocating.clear();

        // Space freed during this checkpoint is now available for reallocation.
        for (&start, &size) in self.freeing.iter() {
            self.allocating.verify_absent(start, size);
            spacemap.free(start, size);
            self.allocatable.add(start, size);
        }
        self.freeing.clear();
    }

    fn condense_to_spacemap(&self, spacemap: &mut SpaceMap) {
        let mut alloc_offset = self.slab_offset;
        for (&start, &size) in self.allocatable.iter() {
            spacemap.alloc(alloc_offset, start - alloc_offset);
            alloc_offset = start + size;
        }
        spacemap.alloc(
            alloc_offset,
            (self.total_space + self.slab_offset) - alloc_offset,
        );

        // In our attempt to make this independent of flush_to_spacemap(), we do
        // not mutate any of the in-memory data structures and mark all entries
        // from the allocating tree as free. The latter is because these entries
        // will be later marked as allocated in flush_to_spacemap().
        for (&start, &size) in self.allocating.iter() {
            self.allocatable.verify_absent(start, size);
            spacemap.free(start, size);
        }
    }

    fn get_max_size(&self) -> u32 {
        self.max_allowed_alloc_size
    }

    fn get_free_space(&self) -> u64 {
        self.allocatable.space()
    }

    fn get_allocated_space(&self) -> u64 {
        self.total_space - self.get_free_space()
    }

    fn get_phys_type(&self) -> SlabPhysType {
        SlabPhysType::ExtentBased {
            max_size: self.max_allowed_alloc_size,
        }
    }
}

struct FreeSlab {}

impl FreeSlab {
    fn new_slab(id: SlabId, generation: u64) -> Slab {
        Slab::new(id, generation, SlabType::Free(FreeSlab {}))
    }
}

impl SlabTrait for FreeSlab {
    fn import_alloc(&mut self, extent: Extent) {
        panic!("attempting to import alloc {:?} on free slab", extent);
    }

    fn import_free(&mut self, extent: Extent) {
        panic!("attempting to import free {:?} on free slab", extent);
    }

    fn allocate(&mut self, size: u32) -> Option<Extent> {
        panic!(
            "attempting to allocate block from free slab: size = {}",
            size
        );
    }

    fn free(&mut self, extent: Extent) {
        panic!("attempting to free block from free slab: {:?}", extent);
    }

    fn flush_to_spacemap(&mut self, _: &mut SpaceMap) {
        panic!("attempting to flush free slab",);
    }

    fn condense_to_spacemap(&self, _: &mut SpaceMap) {
        // Nothing to condense for free slabs
    }

    fn get_max_size(&self) -> u32 {
        panic!("free slab doesn't have a maximum allocation size");
    }

    fn get_free_space(&self) -> u64 {
        panic!("free slab doesn't have free space");
    }

    fn get_allocated_space(&self) -> u64 {
        panic!("free slab doesn't have allocated space");
    }

    fn get_phys_type(&self) -> SlabPhysType {
        SlabPhysType::Free
    }
}

enum SlabType {
    BitmapBased(BitmapSlab),
    ExtentBased(ExtentSlab),
    Free(FreeSlab),
}

impl SlabType {
    fn with_trait_mut<R, F>(&mut self, cb: F) -> R
    where
        F: FnOnce(&mut dyn SlabTrait) -> R,
    {
        match self {
            SlabType::BitmapBased(t) => cb(t),
            SlabType::ExtentBased(t) => cb(t),
            SlabType::Free(t) => cb(t),
        }
    }

    fn with_trait<R, F>(&self, cb: F) -> R
    where
        F: FnOnce(&dyn SlabTrait) -> R,
    {
        match self {
            SlabType::BitmapBased(t) => cb(t),
            SlabType::ExtentBased(t) => cb(t),
            SlabType::Free(t) => cb(t),
        }
    }
}

struct Slab {
    id: SlabId,
    generation: u64,
    info: SlabType,
    is_dirty: bool,
}

impl Slab {
    fn new(id: SlabId, generation: u64, info: SlabType) -> Slab {
        Slab {
            id,
            generation,
            info,
            is_dirty: false,
        }
    }

    fn import_alloc(&mut self, extent: Extent) {
        self.info.with_trait_mut(|t| t.import_alloc(extent));
    }

    fn import_free(&mut self, extent: Extent) {
        self.info.with_trait_mut(|t| t.import_free(extent));
    }

    fn allocate(&mut self, size: u32) -> Option<Extent> {
        self.info.with_trait_mut(|t| t.allocate(size))
    }

    fn free(&mut self, extent: Extent) {
        self.info.with_trait_mut(|t| t.free(extent));
    }

    fn flush_to_spacemap(&mut self, spacemap: &mut SpaceMap) {
        self.info.with_trait_mut(|t| t.flush_to_spacemap(spacemap));
        self.is_dirty = false;
    }

    fn condense_to_spacemap(&mut self, spacemap: &mut SpaceMap) {
        // Bump the generation of this slab since we are condensing it and
        // writing it to the new spacemap. By bumping the generation we also
        // make the entries in the old spacemap obsolete.
        self.generation += 1;
        spacemap.mark_generation(self.id, self.generation);
        self.info
            .with_trait_mut(|t| t.condense_to_spacemap(spacemap));
    }

    fn get_max_size(&self) -> u32 {
        self.info.with_trait(|t| t.get_max_size())
    }

    fn get_free_space(&self) -> u64 {
        self.info.with_trait(|t| t.get_free_space())
    }

    fn get_allocated_space(&self) -> u64 {
        self.info.with_trait(|t| t.get_allocated_space())
    }

    fn get_phys(&self) -> SlabPhys {
        SlabPhys {
            generation: self.generation,
            slab_type: self.info.with_trait(|t| t.get_phys_type()),
        }
    }

    fn to_sorted_slab_entry(&self) -> SortedSlabEntry {
        SortedSlabEntry {
            allocated_space: self.get_allocated_space(),
            slab_id: self.id,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
struct SortedSlabEntry {
    allocated_space: u64,
    slab_id: SlabId,
}

struct SortedSlabs {
    is_extent_based: bool,
    by_freeness: BTreeSet<SortedSlabEntry>,
    last_allocated: Option<SortedSlabEntry>,
    been_through_once: bool,
}

impl SortedSlabs {
    fn new<I>(is_extent_based: bool, iter: I) -> SortedSlabs
    where
        I: IntoIterator<Item = SortedSlabEntry>,
    {
        let mut by_freeness = BTreeSet::default();
        for x in iter {
            by_freeness.insert(x);
        }
        let last_allocated = by_freeness.iter().next().copied();
        SortedSlabs {
            is_extent_based,
            by_freeness,
            last_allocated,
            been_through_once: false,
        }
    }

    fn get_current(&self) -> Option<SlabId> {
        self.last_allocated.map(|entry| entry.slab_id)
    }

    fn advance(&mut self) -> Option<SlabId> {
        if self.been_through_once {
            // If this clause is hit it means that we've been through all
            // the slabs in this SortedSlab set and we've also filled up
            // a slab that we just created and inserted to the set. In
            // order to not iterate through all the slabs again for this
            // checkpoint we set last_allocated to None and return that.
            self.last_allocated = None;
        }

        if let Some(last_allocated) = self.last_allocated {
            self.last_allocated = self
                .by_freeness
                .range((Excluded(last_allocated), Unbounded))
                .next()
                .copied();
        }

        if self.last_allocated.is_none() {
            // We've iterated through all the existing slabs in
            // the SortedSlab set for this checkpoint. This flag
            // will be reset when we re-create this SortedSlabs
            // at the end of the checkpoint.
            self.been_through_once = true;
        }
        self.get_current()
    }

    fn insert(&mut self, sorted_slab_entry: SortedSlabEntry) {
        self.by_freeness.insert(sorted_slab_entry);
        self.last_allocated = Some(sorted_slab_entry);
    }
}

// key - max allocation that this set of slabs can satisfy
// value - the set of sorted slabs
//
// Note: Even though not strictly necessary, in general
// the BitmapBased slabs are before all the ExtentBased
// ones (i.e. Bitmaps are used for smaller allocation sizes).
struct SlabAllocationBuckets(BTreeMap<u32, SortedSlabs>);

impl SlabAllocationBuckets {
    fn new(phys: SlabAllocationBucketsPhys) -> Self {
        let mut buckets = BTreeMap::new();
        for (max_size, is_extent_based) in phys.buckets {
            buckets.insert(max_size, SortedSlabs::new(is_extent_based, iter::empty()));
        }
        SlabAllocationBuckets(buckets)
    }

    fn add_slab_to_bucket(&mut self, bucket: u32, slab: &Slab) {
        self.0
            .get_mut(&bucket)
            .unwrap()
            .insert(slab.to_sorted_slab_entry());
    }

    fn get_bucket_for_allocation_size(&mut self, request_size: u32) -> (&u32, &mut SortedSlabs) {
        self.0
            .range_mut((Included(request_size), Unbounded))
            .next()
            .expect("allocation request larger than largest configured slab type")
    }
}

struct Slabs(Vec<Slab>);

impl Slabs {
    fn get(&self, id: SlabId) -> &Slab {
        &self.0[id.as_index()]
    }

    fn get_mut(&mut self, id: SlabId) -> &mut Slab {
        &mut self.0[id.as_index()]
    }
}

pub struct BlockAllocator {
    coverage: Extent,
    slab_size: u32,

    // # Spacemap Condensing - Design Overview
    //
    // We need to condense our spacemap in order to not run out of space.
    //
    // In a scheme where one spacemap is used to log the changes from all
    // the slabs, condensing would be expensive for workloads where there
    // are lots of incoming allocations/frees because these changes would
    // need to wait for condensing to be done before they are applied.
    //
    // On the other hand, having one spacemap per slab and choosing how
    // many of them to condense dynamically based on the workload could
    // be a viable option. Unfortunately, it comes with its own set of
    // problems too. Specifically, for big devices that have a lot of
    // slabs with a small amount of pending changes each, condensing would
    // cause scattered I/0s whose block size won't be fully utilized,
    // affecting our overall bandwidth as a result.
    //
    // The above antithetical designs highlight a tension in the number
    // of spacemaps we choose to represent our slabs and the problems that
    // come up if you have too many or too little of them. Picking the
    // right number of spacemaps is hard, primarily because that number
    // is workload dependend and dynamically changing it is not something
    // that can be done in a straightforward manner.
    //
    // For this block allocator we decided to approach things differently.
    // We use a two spacemap scheme (`spacemap` and `spacemap_next`) where
    // a certain number of slabs are condensed in a round-robin fashion every
    // checkpoint. Initially all slabs flush their changes to the first
    // spacemap (`spacemap`). Whenever a slab is condensed, we place its
    // condensed entries/representation to the second spacemap (`spacemap_next`).
    // Every subsequent changes/flushes for that slab are also placed on that
    // spacemap. Once we've done a full circle and all slabs have been moved
    // to `spacemap_next`, then `spacemap` is no longer needed. At that point
    // we get rid of `spacemap`, replacing it with `spacemap_next`, and
    // use an empty spacemap as `spacemap_next` for our next round of condensing.
    //
    // With the above design we use at most 2 I/Os where we expect the blocksize
    // to be utilized as the two spacemaps represent all the slabs in the
    // Zettacache. Furthermore, we can dynamically adjust the condensing rate
    // (TODO: see DOSE-629) however we see fit, making sure that our spacemaps
    // don't grow too long and that condensing itself doesn't interfere too much
    // with other activity.
    spacemap: SpaceMap,
    spacemap_next: SpaceMap,
    next_slab_to_condense: SlabId,

    slabs: Slabs,
    dirty_slabs: Vec<SlabId>,
    free_slabs: Vec<SlabId>,

    slab_buckets: SlabAllocationBuckets,

    available_space: u64,
    freeing_space: u64,

    block_access: Arc<BlockAccess>,
    extent_allocator: Arc<ExtentAllocator>,
}

impl BlockAllocator {
    pub async fn open(
        block_access: Arc<BlockAccess>,
        extent_allocator: Arc<ExtentAllocator>,
        phys: BlockAllocatorPhys,
    ) -> BlockAllocator {
        let spacemap = SpaceMap::open(
            block_access.clone(),
            extent_allocator.clone(),
            phys.spacemap,
        );
        let spacemap_next = SpaceMap::open(
            block_access.clone(),
            extent_allocator.clone(),
            phys.spacemap_next,
        );

        let coverage = spacemap.get_coverage();
        let slab_size = phys.slab_size;
        let num_slabs = usize::from64(coverage.size / u64::from(slab_size));
        assert_eq!(num_slabs, phys.slabs.0.len());
        let mut available_space = 0u64;

        let mut slabs_vec = Vec::with_capacity(num_slabs);
        for (slab_id, phys_slab) in phys.slabs.0.iter().enumerate() {
            // ensure that we are pushing at the right offset of the Vec
            assert_eq!(slab_id, slabs_vec.len());

            let sid = SlabId(slab_id as u64);
            let slab_offset = coverage.location.offset + (u64::from(slab_size) * num_slabs as u64)
                - (sid.0 + 1) * u64::from(slab_size);
            match phys_slab.slab_type {
                SlabPhysType::BitmapBased { block_size } => {
                    slabs_vec.push(BitmapSlab::new_slab(
                        sid,
                        phys_slab.generation,
                        slab_offset,
                        phys.slab_size,
                        block_size,
                    ));
                }
                SlabPhysType::ExtentBased { max_size } => {
                    slabs_vec.push(ExtentSlab::new_slab(
                        sid,
                        phys_slab.generation,
                        slab_offset,
                        phys.slab_size,
                        max_size,
                    ));
                }
                SlabPhysType::Free => {
                    slabs_vec.push(FreeSlab::new_slab(sid, phys_slab.generation));
                }
            }
        }
        assert_eq!(slabs_vec.len(), num_slabs);

        let mut slabs = Slabs(slabs_vec);
        let mut slab_import_generations = vec![0u64; num_slabs];
        let mut import_cb = |entry| match entry {
            SpaceMapEntry::Alloc(extent) => {
                let extent = Extent {
                    location: DiskLocation {
                        offset: extent.offset,
                    },
                    size: extent.size,
                };
                let slab_id = BlockAllocator::slab_id_from_extent_impl(
                    coverage.location.offset,
                    slab_size,
                    num_slabs as u64,
                    extent,
                );
                if slabs.get(slab_id).generation == slab_import_generations[slab_id.as_index()] {
                    slabs.get_mut(slab_id).import_alloc(extent)
                }
            }
            SpaceMapEntry::Free(extent) => {
                let extent = Extent {
                    location: DiskLocation {
                        offset: extent.offset,
                    },
                    size: extent.size,
                };
                let slab_id = BlockAllocator::slab_id_from_extent_impl(
                    coverage.location.offset,
                    slab_size,
                    num_slabs as u64,
                    extent,
                );
                if slabs.get(slab_id).generation == slab_import_generations[slab_id.as_index()] {
                    slabs.get_mut(slab_id).import_free(extent)
                }
            }
            SpaceMapEntry::MarkGeneration(mark) => {
                assert_ge!(
                    mark.generation,
                    slab_import_generations[mark.slab_id.as_index()]
                );
                slab_import_generations[mark.slab_id.as_index()] = mark.generation;
            }
        };
        spacemap.load(&mut import_cb).await;
        spacemap_next.load(&mut import_cb).await;

        let mut free_slabs = Vec::new();
        let mut slab_buckets = SlabAllocationBuckets::new(phys.slab_buckets);
        for slab in slabs.0.iter() {
            match &slab.info {
                SlabType::BitmapBased(_) | SlabType::ExtentBased(_) => {
                    slab_buckets.add_slab_to_bucket(slab.get_max_size(), slab);
                    available_space += slab.get_free_space();
                }
                SlabType::Free(_) => {
                    free_slabs.push(slab.id);
                    available_space += u64::from(slab_size);
                }
            }
        }

        BlockAllocator {
            coverage,
            slab_size,
            spacemap,
            spacemap_next,
            next_slab_to_condense: phys.next_slab_to_condense,
            slabs,
            dirty_slabs: Default::default(),
            free_slabs,
            slab_buckets,
            available_space,
            freeing_space: 0,
            block_access,
            extent_allocator,
        }
    }

    fn dirty_slab_id(&mut self, slab_id: SlabId) {
        let slab = self.slabs.get_mut(slab_id);
        if !slab.is_dirty {
            self.dirty_slabs.push(slab_id);
            slab.is_dirty = true;
        }
    }

    fn allocate_from_new_slab(&mut self, request_size: u32) -> Option<Extent> {
        let slab_size = self.slab_size;
        let new_id = match self.free_slabs.pop() {
            Some(id) => id,
            None => {
                return None;
            }
        };
        let slab_offset = self.slab_offset_from_slab_id(new_id);
        let slab_next_generation = self.slabs.get(new_id).generation + 1;

        let (&max_allocation_size, sorted_slabs) = self
            .slab_buckets
            .get_bucket_for_allocation_size(request_size);

        let mut new_slab = if sorted_slabs.is_extent_based {
            ExtentSlab::new_slab(
                new_id,
                slab_next_generation,
                slab_offset,
                slab_size,
                max_allocation_size,
            )
        } else {
            BitmapSlab::new_slab(
                new_id,
                slab_next_generation,
                slab_offset,
                slab_size,
                max_allocation_size,
            )
        };
        let target_spacemap = if self.next_slab_to_condense <= new_id {
            &mut self.spacemap
        } else {
            &mut self.spacemap_next
        };
        target_spacemap.mark_generation(new_id, slab_next_generation);
        sorted_slabs.insert(new_slab.to_sorted_slab_entry());

        let extent = new_slab.allocate(request_size);
        assert!(extent.is_some());
        assert!(matches!(self.slabs.get(new_id).info, SlabType::Free(_)));
        *self.slabs.get_mut(new_id) = new_slab;
        self.dirty_slab_id(new_id);
        trace!(
            "BLOCK-ALLOCATOR: {:?} added to {} byte bucket",
            new_id,
            max_allocation_size,
        );
        self.available_space -= extent.unwrap().size;
        extent
    }

    pub fn allocate(&mut self, request_size: u32) -> Option<Extent> {
        assert_ge!(self.slab_size, request_size);

        // Note: we assume allocation sizes are guaranteed to be aligned
        // from the caller for now.
        assert_eq!(
            request_size,
            self.block_access.round_up_to_sector(request_size)
        );

        let (&max_allocation_size, sorted_slabs) = self
            .slab_buckets
            .get_bucket_for_allocation_size(request_size);
        let slabs_in_bucket = sorted_slabs.by_freeness.len();

        // TODO - WIP Allocation Algorithm
        //
        // The current naive implemenation of the allocation is the following:
        // - We are iterating over the slabs of the our allocation bucket in
        //   sorted order from the slabs with the most free space to the ones
        //   with the least free space (according to their free space accounting
        //   since our latest flush/checkpoint).
        // - We are looking at the current slab used since our last allocation
        //   (or the first slab if this is the first allocation since the last
        //    checkpoint), and try to allocate from that.
        // - If the allocation fails we move to the next slab in our set of
        //   sorted slabs, and try to allocate from that one.
        // - If that fails too, we keep trying through all the slabs in that
        //   set until we go through them all at which point we will try to
        //   convert a FreeSlab to this type, add it to the set, and allocate
        //   from it.
        // - If that fails too then we fail the allocation (and any allocation
        //   for that allocation size until the next flush/checkpoint).
        //
        // Obviously this is far from ideal but it is deterministic and easy
        // to reason about for now.
        //
        loop {
            match sorted_slabs.get_current() {
                Some(id) => match self.slabs.get_mut(id).allocate(request_size) {
                    Some(extent) => {
                        trace!(
                            "BLOCK-ALLOCATOR: satisfied {} byte allocation request: {:?}",
                            request_size,
                            extent
                        );
                        self.dirty_slab_id(id);
                        self.available_space -= extent.size;
                        return Some(extent);
                    }
                    None => {
                        let debug = sorted_slabs.advance();
                        trace!(
                            "BLOCK-ALLOCATOR: advance slab bucket {} cursor to {:?}",
                            max_allocation_size,
                            debug
                        );
                    }
                },
                None => match self.allocate_from_new_slab(request_size) {
                    Some(extent) => {
                        trace!(
                            "BLOCK-ALLOCATOR: satisfied {} byte allocation request: {:?}",
                            request_size,
                            extent
                        );
                        return Some(extent);
                    }
                    None => {
                        debug!(
                            "BLOCK-ALLOCATOR: allocation of {} bytes failed; no free slabs left; {} slabs used for {} byte bucket",
                            request_size,
                            slabs_in_bucket,
                            max_allocation_size
                        );
                        return None;
                    }
                },
            }
        }
    }

    pub fn free(&mut self, extent: Extent) {
        assert_eq!(
            extent.size,
            self.block_access.round_up_to_sector(extent.size)
        );
        trace!("BLOCK-ALLOCATOR: free request: {:?}", extent);

        let slab_id = self.slab_id_from_extent(extent);
        self.slabs.get_mut(slab_id).free(extent);
        self.freeing_space += extent.size;
        self.dirty_slab_id(slab_id);
    }

    pub async fn flush(&mut self) -> BlockAllocatorPhys {
        // We first condense any slabs so later when we flush any of them that
        // are dirty we've already migrated their entries of this checkpoint to
        // spacemap_next.
        let begin = Instant::now();
        let slabs_to_condense = min(
            *SLAB_CONDENSE_PER_CHECKPOINT,
            (self.slabs.0.len() - self.next_slab_to_condense.as_index()) as u64,
        );
        trace!(
            "BLOCK-ALLOCATOR: condensing the next {} slabs starting from {:?}",
            slabs_to_condense,
            self.next_slab_to_condense
        );
        for _ in 0..slabs_to_condense {
            self.slabs
                .get_mut(self.next_slab_to_condense)
                .condense_to_spacemap(&mut self.spacemap_next);
            self.next_slab_to_condense = self.next_slab_to_condense.next();
        }
        if self.next_slab_to_condense.as_index() == self.slabs.0.len() {
            self.next_slab_to_condense = SlabId(0);
            let coverage = self.spacemap.get_coverage();
            let new_spacemap = SpaceMap::open(
                self.block_access.clone(),
                self.extent_allocator.clone(),
                SpaceMapPhys::new(coverage.location.offset, coverage.size),
            );
            self.spacemap = mem::replace(&mut self.spacemap_next, new_spacemap);
        }
        assert_lt!(self.next_slab_to_condense.as_index(), self.slabs.0.len());
        trace!(
            "BLOCK-ALLOCATOR: spacemap has {} alloc and {} total entries",
            self.spacemap.get_alloc_entries(),
            self.spacemap.get_total_entries()
        );
        trace!(
            "BLOCK-ALLOCATOR: spacemap_next has {} alloc and {} total entries",
            self.spacemap_next.get_alloc_entries(),
            self.spacemap_next.get_total_entries()
        );

        // Flush any dirty slabs. If any slab is completely empty mark it as free.
        // Keep track of the buckets/SortedSlabs sets that these dirty slabs belong
        // to so later we can update their slab order by freeness.
        trace!(
            "BLOCK-ALLOCATOR: flushing {} dirty slabs",
            self.dirty_slabs.len()
        );
        let mut dirty_buckets = HashSet::new();
        for slab_id in std::mem::take(&mut self.dirty_slabs) {
            let slab = self.slabs.get_mut(slab_id);
            let target_spacemap = if self.next_slab_to_condense <= slab_id {
                &mut self.spacemap
            } else {
                &mut self.spacemap_next
            };
            slab.flush_to_spacemap(target_spacemap);
            dirty_buckets.insert(slab.get_max_size());
            if slab.get_free_space() == u64::from(self.slab_size) {
                self.free_slabs.push(slab.id);
                *slab = FreeSlab::new_slab(slab_id, slab.generation + 1);
                target_spacemap.mark_generation(slab.id, slab.generation);
            }
        }

        trace!(
            "BLOCK-ALLOCATOR: allocation buckets to be resorted: {:?}",
            dirty_buckets
        );

        // Update any buckets which we've performed any allocations/frees during
        // this checkpoint by recreating their SortedSlabs (which in turn
        // updates their order by freeness and also removes any empty slabs).
        for bucket_size in dirty_buckets {
            let bucket = self.slab_buckets.0.get_mut(&bucket_size).unwrap();
            let slabs = &mut self.slabs;
            let iter = bucket.by_freeness.iter().filter_map(|entry| {
                let slab = slabs.get(entry.slab_id);
                if let SlabType::Free(_) = slab.info {
                    None
                } else {
                    Some(slab.to_sorted_slab_entry())
                }
            });
            *bucket = SortedSlabs::new(bucket.is_extent_based, iter);
        }
        self.available_space += self.freeing_space;
        self.freeing_space = 0;

        let phys = BlockAllocatorPhys {
            slab_size: self.slab_size,
            spacemap: self.spacemap.flush().await,
            spacemap_next: self.spacemap_next.flush().await,
            next_slab_to_condense: self.next_slab_to_condense,
            slabs: self
                .slabs
                .0
                .iter()
                .map(|slab| slab.get_phys())
                .collect::<Vec<_>>()
                .into(),
            slab_buckets: SlabAllocationBucketsPhys {
                buckets: self
                    .slab_buckets
                    .0
                    .iter()
                    .map(|(&bucket_size, bucket)| (bucket_size, bucket.is_extent_based))
                    .collect(),
            },
        };
        debug!(
            "flushed BlockAllocator in {}ms",
            begin.elapsed().as_millis()
        );
        phys
    }

    pub fn get_available(&self) -> u64 {
        self.available_space
    }

    pub fn get_freeing(&self) -> u64 {
        self.freeing_space
    }

    //
    // |----------------| Device Offset 0
    // |... metadata ...|
    // |----------------| coverage.offset
    // |                |
    // |     ......     |  ....
    // |                |
    // |----------------|
    // |                | Slab n
    // |----------------|
    // |     ......     |  ....
    // |----------------|
    // |                | Slab 1
    // |----------------|
    // |                | Slab 0
    // |----------------| coverage.offset + (slab_size * slabs.len())
    // |     ......     |  ....
    //
    fn slab_id_from_extent_impl(
        coverage_start: u64,
        slab_size: u32,
        num_slabs: u64,
        extent: Extent,
    ) -> SlabId {
        let id =
            (num_slabs - 1) - ((extent.location.offset - coverage_start) / u64::from(slab_size));
        assert_lt!(id, num_slabs);
        SlabId(id)
    }

    fn slab_id_from_extent(&self, extent: Extent) -> SlabId {
        let slab_size64 = u64::from(self.slab_size);
        let num_slabs = self.slabs.0.len() as u64;
        assert_le!(extent.size, slab_size64);

        let slab_id = BlockAllocator::slab_id_from_extent_impl(
            self.coverage.location.offset,
            self.slab_size,
            num_slabs,
            extent,
        );

        // check all boundaries now before proceeding
        let slab_offset = self.slab_offset_from_slab_id(slab_id);
        assert_ge!(extent.location.offset, slab_offset);
        assert_lt!(extent.location.offset, slab_offset + slab_size64);
        assert_le!(
            extent.location.offset + extent.size,
            slab_offset + slab_size64
        );

        slab_id
    }

    fn slab_offset_from_slab_id(&self, slab_id: SlabId) -> u64 {
        let slab_sz = u64::from(self.slab_size);
        let num_slabs = self.slabs.0.len() as u64;
        self.coverage.location.offset + (slab_sz * num_slabs) - (slab_id.0 + 1) * slab_sz
    }
}

#[derive(Debug, Serialize, Deserialize, Clone)]
enum SlabPhysType {
    BitmapBased { block_size: u32 },
    ExtentBased { max_size: u32 },
    Free,
}
impl OnDisk for SlabPhysType {}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct SlabPhys {
    generation: u64,
    slab_type: SlabPhysType,
}
impl OnDisk for SlabPhys {}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct SlabAllocationBucketsPhys {
    // Buckets sorted by max-allocation-size (ascending-order)
    // (max allocation size, is extent based)
    buckets: Vec<(u32, bool)>,
}
impl OnDisk for SlabAllocationBucketsPhys {}

impl SlabAllocationBucketsPhys {
    fn default() -> Self {
        let mut buckets = Vec::new();
        // Create the first few buckets for bitmap-based slab use
        for b in 0..(16 * 1024 / 512) {
            buckets.push((b * 512, false));
        }
        // Create a few more extent-based buckets for larger sizes
        buckets.push((64 * 1024, true));
        buckets.push((256 * 1024, true));
        buckets.push((1024 * 1024, true));
        buckets.push((*DEFAULT_SLAB_SIZE, true));

        SlabAllocationBucketsPhys { buckets }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct BlockAllocatorPhys {
    slab_size: u32,

    spacemap: SpaceMapPhys,
    spacemap_next: SpaceMapPhys,
    next_slab_to_condense: SlabId,

    // TODO: if this is too big to be writing every checkpoint,
    //       we could use a BlockBasedLog<(SlabId, SlabPhysType)>
    slabs: TerseVec<SlabPhys>,
    slab_buckets: SlabAllocationBucketsPhys,
}
impl OnDisk for BlockAllocatorPhys {}

impl BlockAllocatorPhys {
    // TODO: eventually change this to indicate the size of each
    //       of the disks that we're managing
    pub fn new(offset: u64, size: u64) -> BlockAllocatorPhys {
        let slab_size = *DEFAULT_SLAB_SIZE;
        let num_slabs = size / u64::from(slab_size);
        let mut slabs = Vec::new();
        for _ in range(0, num_slabs) {
            slabs.push(SlabPhys {
                generation: 0,
                slab_type: SlabPhysType::Free,
            });
        }

        BlockAllocatorPhys {
            slab_size,
            spacemap: SpaceMapPhys::new(offset, size),
            spacemap_next: SpaceMapPhys::new(offset, size),
            next_slab_to_condense: SlabId(0),
            slabs: slabs.into(),
            slab_buckets: DEFAULT_SLAB_BUCKETS.clone(),
        }
    }
}

pub async fn zcachedb_dump_spacemaps(
    block_access: Arc<BlockAccess>,
    extent_allocator: Arc<ExtentAllocator>,
    phys: BlockAllocatorPhys,
) {
    println!("DUMP SPACEMAP");
    println!("{:?}", phys.spacemap);
    let spacemap = SpaceMap::open(
        block_access.clone(),
        extent_allocator.clone(),
        phys.spacemap,
    );
    spacemap.load(|entry| println!("{:?}", entry)).await;

    println!();

    println!("DUMP SPACEMAP_NEXT");
    println!("{:?}", phys.spacemap_next);
    let spacemap_next = SpaceMap::open(
        block_access.clone(),
        extent_allocator.clone(),
        phys.spacemap_next,
    );
    spacemap_next.load(|entry| println!("{:?}", entry)).await;
}

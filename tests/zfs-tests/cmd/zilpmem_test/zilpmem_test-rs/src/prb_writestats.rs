use bindings::size_t;

#[derive(
    derive_more::AddAssign,
    derive_more::Sum,
    derive_more::Add,
    Default,
    Clone,
    Copy,
    serde::Serialize,
)]
#[repr(C)]
pub struct prb_write_stats<T: Default + Clone + Copy + serde::Serialize> {
    pub get_committer_slot_nanos: T,
    pub put_committer_slot_nanos: T,
    pub dt_sl_aquisition_nanos: T,
    pub dt_sl_held_nanos: T,
    pub pmem_nanos: T,

    pub get_chunk_calls: size_t,
    pub get_chunk_calls_sleeps: size_t,
    pub obsolete: size_t,
    pub beginning_new_gen: size_t,
    pub committer_slot: size_t,
}

impl From<bindings::prb_write_stats> for prb_write_stats<u64> {
    fn from(o: bindings::prb_write_stats) -> Self {
        let bindings::prb_write_stats {
            get_committer_slot_nanos,
            put_committer_slot_nanos,
            dt_sl_aquisition_nanos,
            dt_sl_held_nanos,
            pmem_nanos,
            get_chunk_calls,
            get_chunk_calls_sleeps,
            obsolete,
            beginning_new_gen,
            committer_slot,
            // the following fields are not carried over because they are not relevant for aggregation
            entry_chunk: _entry_chunk,
            entry_pmem_base: _entry_pmem_base,
        } = o;
        prb_write_stats {
            get_committer_slot_nanos,
            put_committer_slot_nanos,
            dt_sl_aquisition_nanos,
            dt_sl_held_nanos,
            pmem_nanos,
            get_chunk_calls,
            get_chunk_calls_sleeps,
            obsolete,
            beginning_new_gen,
            committer_slot,
        }
    }
}

impl prb_write_stats<u64> {
    pub fn as_f64(&self) -> prb_write_stats<f64> {
        prb_write_stats {
            get_chunk_calls: self.get_chunk_calls,
            get_chunk_calls_sleeps: self.get_chunk_calls_sleeps,
            obsolete: self.obsolete,
            beginning_new_gen: self.beginning_new_gen,
            committer_slot: self.committer_slot,

            get_committer_slot_nanos: (self.get_committer_slot_nanos as f64),
            put_committer_slot_nanos: (self.put_committer_slot_nanos as f64),
            dt_sl_aquisition_nanos: (self.dt_sl_aquisition_nanos as f64),
            dt_sl_held_nanos: (self.dt_sl_held_nanos as f64),
            pmem_nanos: (self.pmem_nanos as f64),
        }
    }
}

impl prb_write_stats<f64> {
    pub fn div_nanos_by(&self, other: f64) -> Self {
        prb_write_stats::<f64> {
            get_chunk_calls: self.get_chunk_calls,
            get_chunk_calls_sleeps: self.get_chunk_calls_sleeps,
            obsolete: self.obsolete,
            beginning_new_gen: self.beginning_new_gen,
            committer_slot: self.committer_slot,

            get_committer_slot_nanos: (self.get_committer_slot_nanos / other),
            put_committer_slot_nanos: (self.put_committer_slot_nanos / other),
            dt_sl_aquisition_nanos: (self.dt_sl_aquisition_nanos / other),
            dt_sl_held_nanos: (self.dt_sl_held_nanos / other),
            pmem_nanos: (self.pmem_nanos / other),
        }
    }
    pub fn mul_nanos_by(&self, other: f64) -> Self {
        prb_write_stats::<f64> {
            get_chunk_calls: self.get_chunk_calls,
            get_chunk_calls_sleeps: self.get_chunk_calls_sleeps,
            obsolete: self.obsolete,
            beginning_new_gen: self.beginning_new_gen,
            committer_slot: self.committer_slot,

            get_committer_slot_nanos: (self.get_committer_slot_nanos * other),
            put_committer_slot_nanos: (self.put_committer_slot_nanos * other),
            dt_sl_aquisition_nanos: (self.dt_sl_aquisition_nanos * other),
            dt_sl_held_nanos: (self.dt_sl_held_nanos * other),
            pmem_nanos: (self.pmem_nanos * other),
        }
    }
}

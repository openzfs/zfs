use crate::ZettaCache;

// This file and its data structures exists solely to interact with the zcachedb
// binary generated from the zcdb crate/directory. This way we don't have to
// unnecessarily expose structures like BlockAllocator outside the zettacache
// crate through lib.rs.
//
// TODO: Command to go over slabs and print what is allocated in detail,
// utilization, etc..
//
// TODO: Command that prints statistics of allocation buckets (e.g. a
// histogram of bucketsize to number of slabs and the amount of culmulative
// free space per bucket).
//
// TODO: Command that calculates the condensed size of the existing
// spacemaps and compares it to their actual size.
//
// TODO: Command that gives a frequency count for each key in the operation
// log (e.g. a histogram where the x-axis is the keys and the y-axis is the
// number of entries)
//
// TODO: Command for stats for blockbased logs, printing the average chunk
// size in bytes and the number of entries (next_chunk_offset can tell us
// how big it is).
//
// TODO: Command that prints a histogram of number of blocks of each
// blocksize. Note that this is slightly different than # block in each slab
// bucket, since extent slabs can have different size blocks in them. We
// could use the index for that since the block allocator wouldn't know the
// exact block sizes by that point.
//
// TODO: Leak detection and consistency checks between the block allocator
// and the index.
//
// TODO: Ping Mark for any more command ideas that would be helpful for
// debugging the index.
#[derive(Debug)]
pub enum ZettaCacheDBCommand {
    // TODO: We still need options to explicitly iterate over the index,
    // operation_log, merging_operation, etc..
    // TODO: Need verification option for these structures (e.g. print out error
    // if there is a double-ALLOC on a spacemap).
    DumpStructures(DumpStructuresOptions),
}

#[derive(Debug)]
pub struct DumpStructuresOptions {
    pub dump_defaults: bool,
    pub dump_spacemaps: bool,
}

impl Default for DumpStructuresOptions {
    fn default() -> Self {
        Self::new()
    }
}

impl DumpStructuresOptions {
    pub fn new() -> Self {
        DumpStructuresOptions {
            dump_defaults: true,
            dump_spacemaps: false,
        }
    }

    pub fn defaults(mut self, value: bool) -> Self {
        self.dump_defaults = value;
        self
    }

    pub fn spacemaps(mut self, value: bool) -> Self {
        self.dump_spacemaps = value;
        self
    }
}

impl ZettaCacheDBCommand {
    pub async fn issue_command(command: ZettaCacheDBCommand, path: &str) {
        match command {
            ZettaCacheDBCommand::DumpStructures(opts) => {
                ZettaCache::zcachedb_dump_structures(path, opts).await
            }
        }
    }
}

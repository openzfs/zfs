# LZ4 in OpenZFS

`lib/` contains the LZ4 library from the upstream LZ4 project
(https://github.com/lz4/lz4), release v1.10.0, under the BSD 2-Clause
license in `lib/LICENSE`. Apart from the SPDX tag on the first line of
`lz4.c` and `lz4.h`, the files are unmodified. Please do not alter them
other than by updating to a newer LZ4 release.

`zfs_lz4.c` is the OpenZFS glue. It builds the library in that single
translation unit, with internal linkage for all of it and without the
functions that allocate memory, and frames each block as OpenZFS always
has: a 4-byte big-endian length, then the LZ4 block.

## Update policy

The LZ4 block format does not change between releases, so any LZ4
decoder reads blocks written by any LZ4 compressor. The exact compressed
bytes may change, though. A block written before an update and one
written after it from the same data then differ, which affects
deduplication, NOP writes and anything that compresses a block read
from disk again to match its checksum: L2ARC writes with compressed ARC
disabled, and corrective receive of a non-raw stream. LZ4 is therefore
updated under the same rules as Zstd, described in
`module/zstd/README.md`.

## Updating LZ4

1. Copy `lz4.c`, `lz4.h` and `LICENSE` from the `lib/` directory of the
   release to `lib/`, unmodified, in a commit of their own.
2. Add the SPDX tags back in a follow-up commit.
3. Build the kernel modules and userspace, and check that `zfs_lz4.o`
   still defines no global symbol other than `lz4_init`, `lz4_fini`,
   `zfs_lz4_compress`, `zfs_lz4_decompress` and the `zfs_`-prefixed
   renames in `zfs_lz4.c`.

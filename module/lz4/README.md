# LZ4 in OpenZFS

`lib/` contains the LZ4 library from the upstream LZ4 project
(https://github.com/lz4/lz4), release v1.10.0, under the BSD 2-Clause
license in `lib/LICENSE`. Apart from the SPDX tag on the first line of
`lz4.c` and `lz4.h`, the files are unmodified. Please do not alter them
other than by updating to a newer LZ4 release.

`zfs_lz4.c` is the OpenZFS glue. It builds the library in that single
translation unit, with internal linkage for all of it and without the
functions that allocate memory, and decodes each block as OpenZFS has
always framed it: a 4-byte big-endian length, then the LZ4 block.

OpenZFS uses the library to decompress only. It compresses with its own
copy of the LZ4 r85 compressor, in `module/zfs/lz4_zfs.c`.

## Update policy

The LZ4 block format does not change between releases, so any LZ4
decoder reads blocks written by any LZ4 compressor, and the library can be
updated without changing a byte on disk.

The exact compressed bytes may change between compressors, though. A
block written before such a change and one written after it from the same
data then differ, which affects deduplication, NOP writes and anything
that compresses a block read from disk again to match what is stored:
L2ARC writes and the MAC of authenticated blocks with compressed ARC
disabled, and corrective receive of a non-raw stream. This is why
`module/zfs/lz4_zfs.c` is left as it is, and why compressing with the
library instead would follow the rules for Zstd updates in
`module/zstd/README.md`.

## Updating LZ4

1. Copy `lz4.c`, `lz4.h` and `LICENSE` from the `lib/` directory of the
   release to `lib/`, unmodified, in a commit of their own.
2. Add the SPDX tags back in a follow-up commit.
3. Build the kernel modules and userspace, and check that `zfs_lz4.o`
   still defines no global symbol other than `zfs_lz4_decompress` and the
   `zfs_`-prefixed renames in `zfs_lz4.c`.

# Zstd-On-ZFS Library Manual

## Introduction

This subtree contains the Zstd library used in ZFS. It is heavily cut-down by
dropping any unneeded files, and combined into a single file, but otherwise is
intentionally unmodified. Please do not alter the file containing the zstd
library, besides upgrading to a newer Zstd release.

Tree structure:

* `zfs_zstd.c` are the actual `zfs` kernel module hooks.
* `lib/` contains the unmodified version of the `Zstandard` library
* `zstd-in.c` is our template file for generating the single-file library
* `include/`: This directory contains supplemental includes for platform
  compatibility, which are not expected to be used by ZFS elsewhere in the
  future. Thus we keep them private to Zstd.

## Zstd update policy

Since the exact compressed byte stream may change between Zstd versions, updates
should follow this policy:

1. Zstd may be updated, as needed, after a new .0 release is tagged.
2. Critical patches may be applied up until the next release freeze,
   _potentially_ even updating to a newer upstream version.
3. The Zstd version will not be upgraded within a major release.
4. Multiple Zstd versions are not supported concurrently within a release.
5. The library import commit must be a clean, unmodified upstream import. Any
   OpenZFS-specific integration or local adjustments go into follow-up commits.
6. Release notes should highlight Zstd updates and any expected impact (e.g.
   changes in compression ratio/performance, and differences in compressed byte
   streams which may affect deduplication or NOP writes).

## Updating Zstd

To update Zstd the following steps need to be taken:

1. Grab the latest release of [Zstd](https://github.com/facebook/zstd/releases).
2. Copy the files output by the following script to `module/zstd/lib/`:
   ```
   grep include [path to zstd]/build/single_file_libs/zstd-in.c | awk '{ print $2 }'
   ```
3. Remove debug.c, threading.c, and zstdmt_compress.c.
4. Update Makefiles with resulting file lists.
5. Follow symbol renaming notes in `include/zstd_compat_wrapper.h`.

## Altering Zstd and breaking changes

If Zstd made changes that break compatibility or you need to make breaking
changes to the way we handle Zstd, it is required to maintain backwards
compatibility.

We already save the Zstd version number within the block header to be used
to add future compatibility checks and/or fixes. However, currently it is
not actually used in such a way.

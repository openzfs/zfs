# ZSTD-On-ZFS Library Manual

## Introduction

This subtree contains the ZSTD library used in ZFS. It is heavily cut-down by
dropping any unneeded files, and combined into a single file, but otherwise is
intentionally unmodified. Please do not alter the file containing the zstd
library, besides upgrading to a newer ZSTD release.

Tree structure:

* `zfs_zstd.c` are the actual `zfs` kernel module hooks.
* `lib/` contains the unmodified version of the `Zstandard` library
* `zstd-in.c` is our template file for generating the single-file library
* `include/`: This directory contains supplemental includes for platform
  compatibility, which are not expected to be used by ZFS elsewhere in the
  future. Thus we keep them private to ZSTD.

## Updating ZSTD

To update ZSTD the following steps need to be taken:

1. Grab the latest release of [ZSTD](https://github.com/facebook/zstd/releases).
2. Copy the files output by the following script to `module/zstd/lib/`:
`grep include [path to zstd]/contrib/single_file_libs/zstd-in.c  | awk '{ print $2 }'`
3. Remove debug.c, threading.c, and zstdmt_compress.c.
4. Update Makefiles with resulting file lists.
5. Follow symbol renaming notes in `include/zstd_compat_wrapper.h`

## Altering ZSTD and breaking changes

If ZSTD made changes that break compatibility or you need to make breaking
changes to the way we handle ZSTD, it is required to maintain backwards
compatibility.

We already save the ZSTD version number within the block header to be used
to add future compatibility checks and/or fixes. However, currently it is
not actually used in such a way.

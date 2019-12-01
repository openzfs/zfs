# ZSTD Contrib Library Manual

## Introduction

This `contrib` contains the ZSTD library used in ZFS. It is heavily cut-down by
dropping any unneeded files but otherwise is intentionally unmodified. Please do
not alter these files in any way, besides upgrading to a newer ZSTD release.

## Updating ZSTD

To update ZSTD the following steps need to be taken:

1. Grab the latest release of [ZSTD](https://github.com/facebook/zstd/releases).
2. Replace (not merge) `common`, `compress`, `decompress` and `zstd.h` with the
   new versions from `lib/`.
3. Make sure any newly required files and/or folders are also included.
   1. Add an empty file `Makefile.am` inside any new folder.
   2. Add them to `AC_CONFIG_FILES` in `configure.ac` accordingly.
   3. Make sure new files/folders are listed in
      - `contrib/zstd/Makefile.am`
      - `lib/libzstd/Makefile.am`
      - `module/zstd/Makefile.in`
      - this README
4. Update the version `ZFS_MODULE_VERSION("x.y.z")` in `module/zstd/zstd.c`.

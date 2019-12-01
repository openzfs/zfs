
# ZSTD Contrib Library Manual


## Introduction

ZSTD on ZFS uses a heavily cut down, but otherwise unmodified version of ZSTD.
This `contrib` contains the  unmodified ZSTD library, please do not edit these files.

## Updating ZSTD

To update ZSTD the following steps need to be taken:

1.  Download the latest archive of ZSTD
2. Replace (not merge): `common`, `compress`, `decompress` and `zstd.h` with the new version from the `lib` folder in the downloaded archive of zstd.
3. Make sure any newly requered files and/or folders are also included.
 A. Make sure new folders are listed in: `configure.ac`
 B. Make sure new Files And Folder are listed in: `lib/libzstd/Makefile.am` and `module/zstd/Makefile.in`
 C. Make sure any relevant File and Folders are listed in this Readme
4.  Make sure to change `ZFS_MODULE_VERSION("1.4.4")` in `module/zstd/zstd.c`
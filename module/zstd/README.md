# ZSTD-On-ZFS Library Manual

## Introduction

This subtree contains the ZSTD library used in ZFS. It is heavily cut-down by
dropping any unneeded files, and combined into a single file, but otherwise is
intentionally unmodified. Please do not alter the file containing the zstd
library, besides upgrading to a newer ZSTD release.

Tree structure:

* `zfs_zstd.c` is the actual `zzstd` kernel module.
* `lib/` contains the the unmodified, [_"amalgamated"_](https://github.com/facebook/zstd/blob/dev/contrib/single_file_libs/README.md)
  version of the `Zstandard` library, generated from our template file
* `zstd-in.c` is our template file for generating the library
* `include/`: This directory contains supplemental includes for platform
  compatibility, which are not expected to be used by ZFS elsewhere in the
  future. Thus we keep them private to ZSTD.

## Updating ZSTD

To update ZSTD the following steps need to be taken:

1. Grab the latest release of [ZSTD](https://github.com/facebook/zstd/releases).
2. Update `module/zstd/zstd-in.c` if required. (see
   `zstd/contrib/single_file_libs/zstd-in.c` in the zstd repository)
3. Generate the "single-file-library" and put it to `module/zstd/lib/`.
4. Copy the following files to `module/zstd/lib/`:
   - `zstd/lib/zstd.h`
   - `zstd/lib/common/zstd_errors.h`

This can be done using a few shell commands from inside the zfs repo.
"update_zstd.sh" is included as a scripted example of how to update the ZSTD library.
It should be run for from the ZFS root directory like this:
`sh module/zstd/update_zstd.sh`

### Compatibility Wraper
ZSTD-on-ZFS contains a so-called "Compatibility Wraper".
This wrapper fixes a problem, in case the ZFS filesystem driver, is compiled
staticly into the kernel.
This will cause a symbol collision with the in-kernel zstd library.
The macros contained in 'include/zstd_compat_wrapper.h' will simply
rename all local zstd symbols and references.

If the zstd library for zfs is updated to a newer version, this macro
list usually needs to be updated.
this can be done with some hand crafting of the output of the following
script:
```
nm -fposix zstd.o | awk '{print "#define "$1 " zfs_" $1}' | sort -u > macrotable
```

The output, contained in a file called 'macrotable', still needs a lot of manual editing:
- Removal of .part.number and similair endings
- Removal of non-zstd-library lines
- Wrapping line-ends

_TODO: Add a script to automate some repeatitive bits of this a little more_

## Altering ZSTD and breaking changes

If ZSTD made changes that break compatibility or you need to make breaking
changes to the way we handle ZSTD, it is required to maintain backwards
compatibility.

It's also important to prevent any customisations directly in the library 
file `lib/zstd.c` and instead try other solutions, as this file is generated
from stock zstd.

If you do need to modify the library in 'lib/zstd.c', please be sure to carefully
document it, here or on the wiki. Be aware that new code comments in the library 
might be missed and are not persistant.

We already save the ZSTD version number within the block header to be used
to add future compatibility checks and/or fixes. However, currently it is
not actually used in such a way.

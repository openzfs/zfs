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

This can be done using a few shell commands from inside the zfs repo:

~~~sh
cd PATH/TO/ZFS

url="https://github.com/facebook/zstd"
release="$(curl -s "${url}"/releases/latest | grep -oP '(?<=v)[\d\.]+')"
zstd="/tmp/zstd-${release}/"

wget -O /tmp/zstd.tar.gz \
    "${url}/releases/download/v${release}/zstd-${release}.tar.gz"
tar -C /tmp -xzf /tmp/zstd.tar.gz

cp ${zstd}/lib/zstd.h module/zstd/lib/
cp ${zstd}/lib/zstd_errors.h module/zstd/lib/
${zstd}/contrib/single_file_libs/combine.sh \
    -r ${zstd}/lib -o module/zstd/lib/zstd.c module/zstd/zstd-in.c
~~~

Note: if the zstd library for zfs is updated to a newer version,
the macro list in include/zstd_compat_wrapper.h usually needs to be updated.
this can be done with some hand crafting of the output of the following
script: nm zstd.o | awk '{print "#define "$3 " zfs_" $3}' > macrotable


## Altering ZSTD and breaking changes

If ZSTD made changes that break compatibility or you need to make breaking
changes to the way we handle ZSTD, it is required to maintain backwards
compatibility.

We already save the ZSTD version number within the block header to be used
to add future compatibility checks and/or fixes. However, currently it is
not actually used in such a way.

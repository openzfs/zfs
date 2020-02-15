# ZSTD Contrib Library Manual

## Introduction

This subtree contains the ZSTD library used in ZFS. It is heavily cut-down by
dropping any unneeded files but otherwise is intentionally unmodified. Please do
not alter these files in any way, besides upgrading to a newer ZSTD release.

Tree structure:

* `zstd.c` is the actual `zzstd` kernel module.
* `zstdlib.[hc]` is the unmodified, [_"amalgamated"_](https://github.com/facebook/zstd/blob/dev/contrib/single_file_libs/README.md)
	version of the `Zstandard` library.
* `include`: This directory contains supplemental includes for FreeBSD
	compatibility, which are not expected to be used by ZFS elsewhere in the
	future. Thus we keep them private to ZSTD.

## Updating ZSTD

To update ZSTD the following steps need to be taken:

1. Grab the latest release of [ZSTD](https://github.com/facebook/zstd/releases).
2. Update `module/zstd/zstdlib-in.c` if required. (see
   `zstd/contrib/single_file_libs/zstd-in.c` in the zstd repository)
3. Generate the "single-file-library" and put it together with the header file
	 (`zstd/lib/zstd.h`) to `module/zstd/` as `zstdlib.c` and `zstdlib.h`.

This can be done using a few shell commands from inside the zfs repo:

~~~sh
cd PATH/TO/ZFS

url="https://github.com/facebook/zstd"
release="$(curl -s "${url}"/releases/latest | grep -oP '(?<=v)[\d\.]+')"
zstd="/tmp/zstd-${release}/"

wget -O /tmp/zstd.tar.gz "${url}/releases/download/v${release}/zstd-${release}.tar.gz"
tar -C /tmp -xzf /tmp/zstd.tar.gz

cp ${zstd}/lib/zstd.h module/zstd/zstdlib.h
${zstd}/contrib/single_file_libs/combine.sh \
	-r ${zstd}/lib -o module/zstd/zstdlib.c module/zstd/zstdlib-in.c
~~~

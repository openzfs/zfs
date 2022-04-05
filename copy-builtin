#!/bin/sh

set -ef

usage()
{
	echo "usage: $0 <kernel source tree>" >&2
	exit 1
}

[ "$#" -eq 1 ] || usage
KERNEL_DIR="$1"

if ! [ -e 'zfs_config.h' ]
then
	echo "$0: you did not run configure, or you're not in the ZFS source directory."
	echo "$0: run configure with --with-linux=$KERNEL_DIR and --enable-linux-builtin."

	exit 1
fi >&2

make clean ||:
make gitrev

rm -rf "$KERNEL_DIR/include/zfs" "$KERNEL_DIR/fs/zfs"
cp -R include "$KERNEL_DIR/include/zfs"
cp -R module "$KERNEL_DIR/fs/zfs"
cp zfs_config.h "$KERNEL_DIR/include/zfs/"

cat > "$KERNEL_DIR/fs/zfs/Kconfig" <<EOF
config ZFS
	tristate "ZFS filesystem support"
	depends on EFI_PARTITION
	select ZLIB_INFLATE
	select ZLIB_DEFLATE
	help
	  This is the ZFS filesystem from the OpenZFS project.

	  See https://github.com/openzfs/zfs

	  To compile this file system support as a module, choose M here.

	  If unsure, say N.
EOF

sed -i '/source "fs\/ext2\/Kconfig\"/i\source "fs/zfs/Kconfig"' "$KERNEL_DIR/fs/Kconfig"
echo 'obj-$(CONFIG_ZFS) += zfs/' >> "$KERNEL_DIR/fs/Makefile"

echo "$0: done. now you can build the kernel with ZFS support." >&2
echo "$0: make sure you enable ZFS support (CONFIG_ZFS) before building." >&2

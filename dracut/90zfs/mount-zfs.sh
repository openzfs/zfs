#!/bin/sh

. /lib/dracut-lib.sh

if [ "$rootfs" = "zfs" ]; then
    # parse-zfs.sh should have left our rpool imported and rpool/fs in $root.
    mount -o zfsutil -t "$rootfs" "$root" "$NEWROOT"
    if [ "$?" = "0" ] ; then
        ROOTFS_MOUNTED=yes
    else
        mount -t "$rootfs" "$root" "$NEWROOT" && ROOTFS_MOUNTED=yes
    fi
fi

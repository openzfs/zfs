#!/bin/sh

. /lib/dracut-lib.sh

if [ "$rootfs" = "zfs" ]; then
    # parse-zfs.sh should have left our rpool imported and rpool/fs in $root.
    zfsboot="${root#zfs:}"
    mount -o zfsutil -t "$rootfs" "$zfsboot" "$NEWROOT"
    if [ "$?" = "0" ] ; then
        ROOTFS_MOUNTED=yes
    else
        mount -t "$rootfs" "$zfsboot" "$NEWROOT" && ROOTFS_MOUNTED=yes
    fi
fi

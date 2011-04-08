#!/bin/sh

. /lib/dracut-lib.sh

if [ "$rootfs" = "zfs" ]; then
    zfsrootfs=`echo "$root" | sed 's|^zfs:||'`
    zfspool=`echo "$zfsrootfs" | sed 's|/.*||g'`
    zpool import -N "$zfspool"
    mount -o zfsutil -t "$rootfs" "$zfsrootfs" "$NEWROOT"
    if [ "$?" = "0" ]
    then
        ROOTFS_MOUNTED=yes
    else
        mount -t "$rootfs" "$zfsrootfs" "$NEWROOT" && ROOTFS_MOUNTED=yes
    fi
fi

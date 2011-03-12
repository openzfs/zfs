#!/bin/sh

. /lib/dracut-lib.sh

if [ "$rootfs" = "zfs" ]; then
    zfsrootfs=`echo "$root" | sed 's|^zfs:||'`
    zfspool=`echo "$zfsrootfs" | sed 's|/.*||g'`
    /sbin/zpool import -N "$zfspool"
    mount -t "$rootfs" "$zfsrootfs" "$NEWROOT" && ROOTFS_MOUNTED=yes 
fi

#!/bin/sh

. /lib/dracut-lib.sh

if [ "$rootfs" = "zfs" ]; then
    zfsrootfs=`echo "$root" | sed 's|^zfs:||'`
    zfspool=`echo "$zfsrootfs" | sed 's|/.*||g'`
    echo "Importing ZFS pool $zfspool for root filesystem"
    zpool import -fN "$zfspool"
    echo "Mounting ZFS root filesystem $zfsrootfs"
    if zfs get mountpoint "$zfsrootfs" | grep -q legacy ; then
        mount -t "$rootfs" "$zfsrootfs" "$NEWROOT" && ROOTFS_MOUNTED=yes
    else
        mount -o zfsutil -t "$rootfs" "$zfsrootfs" "$NEWROOT" && ROOTFS_MOUNTED=yes
    fi
fi

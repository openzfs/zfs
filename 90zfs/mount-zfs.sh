#!/bin/sh

. /lib/dracut-lib.sh

if [ -n "$root" -a -z "${root%%zfs:*}" ]; then
    mount -t "$rootfstype" "${root#zfs:}" "$NEWROOT" && ROOTFS_MOUNTED=yes 
fi

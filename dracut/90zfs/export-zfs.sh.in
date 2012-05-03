#!/bin/sh

_do_zfs_shutdown() {

  if [ "`zpool list 2>&1`" != "no pools available" ] ; then

    info "Cleaning up LUKS devices stored in ZVOLs"
    for i in /dev/mapper/luks-*; do
        cryptsetup luksClose $i >/dev/null 2>&1
    done
    dmsetup -v remove_all >/dev/null 2>&1

    info "Unmounting all ZFS file systems"
    zfs unmount -a -f 2>&1 | grep -v /sysroot 1>&2

    info "Unmounting /sysroot if it's mounted"
    umount /sysroot > /dev/null 2>&1

    info "Discovering active ZFS pools"
    local pools=`zpool list -H -o name`
    for pool in $pools ; do
        info "Exporting ZFS pool $pool"
        zpool export $pool # 2>&1 | grep -v /sysroot 1>&2
    done
    unset pools

    info "Cleaning up LUKS devices backing ZFS pools"
    for i in /dev/mapper/luks-*; do
        cryptsetup luksClose $i >/dev/null 2>&1
    done
    dmsetup -v remove_all >/dev/null 2>&1

    local name=`basename "$0"`
    info "ZFS shutdown complete at stage $name"
    unset name

  fi

}

_do_zfs_shutdown $1

#!/bin/sh
set -e

zedd="/usr/lib/zfs-linux/zed.d"
etcd="/etc/zfs/zed.d"

if [ "$1" = "purge" ] && [ -d "$etcd" ] ; then
    # remove the overrides created in prerm
    find "${etcd}" -maxdepth 1 -lname '/dev/null' -delete
    # remove any dangling symlinks to old zedlets
    find "${etcd}" -maxdepth 1 -lname "${zedd}/*" -xtype l  -delete
    # clean up any empty directories
    ( rmdir "$etcd" && rmdir "/etc/zfs" ) || true
fi

#DEBHELPER#


#!/bin/sh

# This is a script to simply unshare all LIO targets, without going through
# ZFS. Mostly used for testing...

SYSFS=/sys/kernel/config/target

cd $SYSFS/iscsi/
if [ -z "$*" ]; then
    targets=`echo iqn.*`
else
    targets=`echo $*`
fi

for name in $targets; do
    tid=`echo $SYSFS/core/iblock_*/$name | sed "s@.*iblock_\(.*\)/.*@\1@"`
    lnk=`find $SYSFS/iscsi/$name/tpgt_$tid/lun/lun_*/* | egrep -v 'alua|statistics' | sed 's@.*/@@'`

    echo 0 > $SYSFS/iscsi/$name/tpgt_$tid/enable

    rmdir $SYSFS/iscsi/$name/tpgt_$tid/np/*
    rm    $SYSFS/iscsi/$name/tpgt_$tid/lun/lun_*/$lnk
    rmdir $SYSFS/iscsi/$name/tpgt_$tid/lun/lun_*
    rmdir $SYSFS/iscsi/$name/tpgt_$tid
    rmdir $SYSFS/iscsi/$name

    rmdir $SYSFS/core/iblock_$tid/$name
    rmdir $SYSFS/core/iblock_$tid
done

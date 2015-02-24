#!/bin/sh

# This is a script to somply unshare all SCST targets, without going through
# ZFS. Mostly used for testing...

SYSFS=/sys/kernel/scst_tgt

cd $SYSFS/targets/iscsi/
if [ -z "$*" ]; then
    targets=`echo iqn.*`
else
    targets=`echo $*`
fi

for name in $targets; do
    find $SYSFS/targets/iscsi/$name/sessions/* -type d > /dev/null 2>&1
    if [ "$?" -eq "1" ]; then
        [ ! -f "$SYSFS/targets/iscsi/$name/enabled" ] && continue

	#scstadmin -noprompt -disable_target $name -driver iscsi
	echo 0 > $SYSFS/targets/iscsi/$name/enabled

	#scstadmin -noprompt -close_dev $dev -handler vdisk_blockio
	dev=`/bin/ls -l $SYSFS/targets/iscsi/$name/luns/0/device | sed 's@.*/@@'`
	echo "del_device $dev" > $SYSFS/handlers/vdisk_blockio/mgmt

	#scstadmin -noprompt -rem_target $name -driver iscsi
	echo "del_target $name" > $SYSFS/targets/iscsi/mgmt
    else
	echo "Can't destroy $name - have sessions"
    fi
done

#!/bin/sh

SYSFS=/sys/kernel/scst_tgt

#for cmd in handler device target driver; do
#    echo $cmd"s: "
#    scstadmin -list_$cmd | egrep -v '^Collecting|^All done.|^$|-----|Handler|Driver'
#    echo
#done

echo -n "Drivers:  "
DRIVERS=`find $SYSFS/targets/* -maxdepth 0 2> /dev/null | sed "s@.*/@@"`
for driver in $DRIVERS; do
    echo -n "$driver "
done
echo

echo -n "Handlers: "
HANDLERS=`find $SYSFS/handlers/*/type 2> /dev/null`
for file in $HANDLERS; do
    handler=`echo "$file" | sed "s@.*handlers/\(.*\)/.*@\1@"`
    echo -n "$handler "
done
echo

echo "Devices:"
DEVICES=`find $SYSFS/devices/*/filename 2> /dev/null`
for file in $DEVICES; do
    device=`echo "$file" | sed "s@.*devices/\(.*\)/.*@\1@"`
    fsdev=`cat "$file" | head -n1`
    handler=`/bin/ls -l $SYSFS/devices/$device/handler  | sed "s@.*handlers/\(.*\)@\1@"`
    blocksize=`head -n1 $SYSFS/devices/$device/blocksize`
    size_mb=`cat $SYSFS/devices/$device/size_mb`

    printf "  %-16s %-16s %-50s %5sB %sMB\n" "$handler" "$device" "$fsdev" "$blocksize" "$size_mb"
done

echo "Targets:"
for driver in $DRIVERS; do
    TARGETS=`find $SYSFS/targets/$driver/iqn*/enabled 2> /dev/null`
    for file in $TARGETS; do
	dir=`dirname "$file"`
	iqn=`echo "$file" | sed "s@.*/iscsi/\(.*\)/.*@\1@"`
	stat=`cat $file`
	if [ "$stat" -eq "1" ]; then
	    tid=`cat $dir/tid`
	    lun=""
	    if [ -d "$SYSFS/targets/$driver/$iqn/luns/0" ]; then
		device=`/bin/ls -l $SYSFS/targets/$driver/$iqn/luns/0/device | sed 's@.*/@@'`
	    fi
	    printf "  %4s  %-7s    %-68s %s\n" "$tid" "$driver" "$iqn" "$device"
	fi
    done
done


#!/bin/sh

ZVER=$(cat /sys/module/zfs/version | cut -f 1 -d '-')
KVER=$(uname -r)

bpftrace \
	--include "/usr/src/zfs-$ZVER/$KVER/zfs_config.h" \
	-I "/usr/src/zfs-$ZVER/include" \
	-I "/usr/src/zfs-$ZVER/include/spl" \
	"$@"

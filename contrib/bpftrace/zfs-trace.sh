#!/bin/sh

ZVER=$(cut -f 1 -d '-' /sys/module/zfs/version)
KVER=$(uname -r)

exec bpftrace \
	--include "/usr/src/zfs-$ZVER/$KVER/zfs_config.h" \
	-I "/usr/src/zfs-$ZVER/include" \
	-I "/usr/src/zfs-$ZVER/include/spl" \
	"$@"

#!/bin/sh

read -r ZVER < /sys/module/zfs/version
ZVER="${ZVER%%-*}"
KVER=$(uname -r)

exec bpftrace \
	--include "/usr/src/zfs-$ZVER/$KVER/zfs_config.h" \
	-I "/usr/src/zfs-$ZVER/include" \
	-I "/usr/src/zfs-$ZVER/include/spl" \
	"$@"

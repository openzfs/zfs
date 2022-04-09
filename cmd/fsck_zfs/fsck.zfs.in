#!/bin/sh
#
# fsck.zfs: A fsck helper to accommodate distributions that expect
# to be able to execute a fsck on all filesystem types.
#
# This script simply bubbles up some already-known-about errors,
# see fsck.zfs(8)
#

if [ $# -eq 0 ]; then
	echo "Usage: $0 [options] dataset…" >&2
	exit 16
fi

ret=0
for dataset; do
	case "$dataset" in
		-*)
			continue
			;;
		*)
			;;
	esac

	pool="${dataset%%/*}"

	case "$(@sbindir@/zpool list -Ho health "$pool")" in
		DEGRADED)
			ret=$(( ret | 4 ))
			;;
		FAULTED)
			awk '!/^([[:space:]]*#.*)?$/ && $1 == "'"$dataset"'" && $3 == "zfs" {exit 1}' /etc/fstab || \
				ret=$(( ret | 8 ))
			;;
		"")
			# Pool not found, error printed by zpool(8)
			ret=$(( ret | 8 ))
			;;
		*)
			;;
	esac
done

exit "$ret"

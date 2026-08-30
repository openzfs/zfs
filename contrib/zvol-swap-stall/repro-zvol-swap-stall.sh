#!/usr/bin/env bash
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
# Destructive reproducer: run only in a disposable VM.

set -o errexit
set -o nounset
set -o pipefail

die()
{
	echo "error: $*" >&2
	exit 2
}

[[ $# == 1 ]] || die 'usage: repro-zvol-swap-stall.sh zvol|raw'
backend=$1
case $backend in zvol|raw) ;; *) die 'backend must be zvol or raw' ;; esac

[[ ${ZVOL_SWAP_VM_GUARD:-} == disposable-vm ]] || \
	die 'set ZVOL_SWAP_VM_GUARD=disposable-vm inside a disposable VM'
(( EUID == 0 )) || die 'must run as root'

pool_device=${POOL_DEVICE:-/dev/disk/by-id/virtio-zvolswap-pool}
raw_swap_device=${RAW_SWAP_DEVICE:-/dev/disk/by-id/virtio-zvolswap-raw}
pool_name=${POOL_NAME:-zvolswap}
swap_mib=${SWAP_MIB:-768}
pressure_extra_mib=${PRESSURE_EXTRA_MIB:-300}
duration=${DURATION:-30}
result_dir=${RESULT_DIR:-/tmp/zvol-swap-minimal}

[[ $pool_device != "$raw_swap_device" ]] || die 'pool and raw devices must differ'
[[ -b $pool_device ]] || die "not a block device: $pool_device"
[[ $backend != raw || -b $raw_swap_device ]] || \
	die "not a block device: $raw_swap_device"
[[ $pool_name =~ ^[a-zA-Z][a-zA-Z0-9_.:-]*$ ]] || die 'unsafe pool name'
[[ $swap_mib =~ ^[1-9][0-9]*$ ]] || die 'SWAP_MIB must be positive'
[[ $pressure_extra_mib =~ ^[1-9][0-9]*$ ]] || \
	die 'PRESSURE_EXTRA_MIB must be positive'
[[ $duration =~ ^[1-9][0-9]*$ ]] || die 'DURATION must be positive'
(( pressure_extra_mib < swap_mib )) || \
	die 'PRESSURE_EXTRA_MIB must be smaller than SWAP_MIB'

for command in awk cat dmesg dirname findmnt grep lsblk mkdir mkswap \
    modinfo modprobe readlink sleep swapon swapoff timeout udevadm uname zfs \
    zpool; do
	command -v "$command" >/dev/null || die "missing command: $command"
done

devices=("$pool_device")
[[ $backend == raw ]] && devices+=("$raw_swap_device")
for device in "${devices[@]}"; do
	findmnt --source "$device" >/dev/null 2>&1 && \
		die "device is mounted: $device"
	descendants=$(lsblk --noheadings --output MOUNTPOINTS "$device" | \
	    grep -v '^$' || true)
	[[ -z $descendants ]] || die "device has mounted descendants: $device"
done

here=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")
mkdir -p "$result_dir" /mnt/zvol-swap
pressure_binary=${PRESSURE_BINARY:-}
if [[ -z $pressure_binary ]]; then
	command -v cc >/dev/null || \
		die 'set PRESSURE_BINARY or install a C compiler'
	pressure_binary=/tmp/zvol-swap-pressure
	cc -std=gnu11 -Wall -Wextra -Werror -O2 -static \
		-o "$pressure_binary" "$here/memory-pressure.c"
fi
[[ -x $pressure_binary ]] || die "not executable: $pressure_binary"

swap_device=
cleanup()
{
	local status=$?
	set +o errexit
	[[ -n $swap_device ]] && timeout 60 swapoff "$swap_device"
	zpool export "$pool_name"
	return "$status"
}
trap cleanup EXIT

modprobe zfs
zpool create -f -o cachefile=none -O compression=off \
	-O mountpoint=/mnt/zvol-swap "$pool_name" "$pool_device"
: >"$result_dir/pool.ready"

if [[ $backend == zvol ]]; then
	zfs create -V "${swap_mib}M" -b 16K -o compression=off \
	    -o primarycache=metadata "$pool_name/swap"
	udevadm settle
	swap_device=/dev/zvol/$pool_name/swap
	for ((attempt = 0; attempt < 5; attempt++)); do
		[[ -b $swap_device ]] && break
		sleep 1
	done
	if [[ ! -b $swap_device ]]; then
		shopt -s nullglob
		candidates=(/dev/zd*)
		shopt -u nullglob
		(( ${#candidates[@]} == 1 )) && swap_device=${candidates[0]}
	fi
else
	swap_device=$raw_swap_device
fi
[[ -b $swap_device ]] || die "swap device did not appear: $swap_device"

if [[ -w /sys/module/zswap/parameters/enabled ]]; then
	echo N >/sys/module/zswap/parameters/enabled
	[[ $(</sys/module/zswap/parameters/enabled) == N ]] || \
		die 'could not disable zswap'
fi
mkswap -f "$swap_device"
swapon "$swap_device"
kernel_swap_device=$(readlink -e "$swap_device")
awk -v device="$kernel_swap_device" \
	'$1 == device { found = 1 } END { exit !found }' /proc/swaps || \
	die 'swap device is not active'
: >"$result_dir/swap.ready"
echo 100 >/proc/sys/vm/swappiness
dmesg -n 7
echo 10 >/proc/sys/kernel/hung_task_timeout_secs
echo 10 >/proc/sys/kernel/hung_task_warnings

mem_total_mib=$(awk '$1 == "MemTotal:" { print int($2 / 1024) }' \
	/proc/meminfo)
pressure_mib=$((mem_total_mib + pressure_extra_mib))
volblocksize=none
if [[ $backend == zvol ]]; then
	volblocksize=$(zfs get -H -o value volblocksize "$pool_name/swap")
fi

cat <<EOF
zvol-swap-minimal backend=$backend
kernel=$(uname -r)
zfs=$(modinfo -F version zfs)
mem_total_mib=$mem_total_mib
swap_mib=$swap_mib
pressure_mib=$pressure_mib
volblocksize=$volblocksize
pressure_log=$result_dir/pressure.log
EOF

"$pressure_binary" "$pressure_mib" "$duration" -1000 "$result_dir" \
	>"$result_dir/pressure.log" 2>&1
dmesg --color=never >"$result_dir/dmesg.log"
cat "$result_dir/pressure.log"

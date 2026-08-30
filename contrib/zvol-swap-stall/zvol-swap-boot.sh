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
# Guest entry point for the destructive workload.  Only the two 9p shares
# named below are mounted; all workload devices are local disposable QEMU
# disks.

set -o errexit
set -o nounset
set -o pipefail

die()
{
	echo "error: $*" >&2
	exit 1
}
(( EUID == 0 )) || die 'must run as root'

finish()
{
	local status=$?
	trap - EXIT
	sync
	poweroff --force >/dev/null 2>&1 || true
	exit "$status"
}
trap finish EXIT

config_mount=/run/zvolconfig
results_mount=/run/zvolresults
mkdir -p "$config_mount" "$results_mount"
mount -t 9p -o trans=virtio,version=9p2000.L zvolconfig "$config_mount"
mount -t 9p -o trans=virtio,version=9p2000.L zvolresults "$results_mount"
: >"$results_mount/guest.started"

config_file=$config_mount/config
[[ -f $config_file ]] || die "missing configuration: $config_file"
while IFS='=' read -r key value || [[ -n $key ]]; do
	[[ -z $key ]] && continue
	[[ $key =~ ^[a-z_]+$ ]] || die "invalid configuration key: $key"
	case $key in
		backend|pool_device|raw_swap_device|pool_name|duration|swap_mib|pressure_extra_mib)
			[[ $value =~ ^[a-zA-Z0-9_./:-]+$ ]] || die "invalid value for $key"
			printf -v "$key" '%s' "$value"
			;;
		*) die "unknown configuration key: $key" ;;
	esac
done <"$config_file"

backend=${backend:-}
pool_device=${pool_device:-}
raw_swap_device=${raw_swap_device:-/dev/disk/by-id/virtio-zvolswap-raw}
pool_name=${pool_name:-zvolswap}
duration=${duration:-30}
swap_mib=${swap_mib:-768}
pressure_extra_mib=${pressure_extra_mib:-300}
case $backend in zvol|raw) ;; *) die 'backend must be zvol or raw' ;; esac
[[ $pool_device == /dev/disk/by-id/virtio-zvolswap-pool ]] || \
	die 'unexpected pool_device'
[[ $raw_swap_device == /dev/disk/by-id/virtio-zvolswap-raw ]] || \
	die 'unexpected raw_swap_device'
[[ $pool_name =~ ^[a-zA-Z][a-zA-Z0-9_.:-]*$ ]] || die 'unsafe pool_name'
[[ $duration =~ ^[1-9][0-9]*$ ]] || die 'duration must be positive'
[[ $swap_mib =~ ^[1-9][0-9]*$ ]] || die 'swap_mib must be positive'
[[ $pressure_extra_mib =~ ^[1-9][0-9]*$ ]] || die 'pressure_extra_mib must be positive'

udevadm settle

result_dir=$results_mount
env ZVOL_SWAP_VM_GUARD=disposable-vm \
	POOL_DEVICE="$pool_device" RAW_SWAP_DEVICE="$raw_swap_device" \
	POOL_NAME="$pool_name" DURATION="$duration" SWAP_MIB="$swap_mib" \
	PRESSURE_EXTRA_MIB="$pressure_extra_mib" RESULT_DIR="$result_dir" \
	PRESSURE_BINARY=/usr/local/libexec/zvol-swap-stall/memory-pressure \
	/usr/local/libexec/zvol-swap-stall/repro-zvol-swap-stall.sh "$backend"

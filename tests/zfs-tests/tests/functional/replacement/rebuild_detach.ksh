#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. "$STF_SUITE/include/libtest.shlib"

#
# DESCRIPTION:
# Replacement cleanup removes repair demand belonging to the detached device.
#
# STRATEGY:
# Sequentially replace a mirror child with automatic verification disabled.
# Delay rebuild reads and wait for persisted progress in the final metaslab.
# Make the old child miss new writes after the last rebuild-range refresh.
# Require automatic detachment without starting a scan on the remaining mirror.
#

verify_runnable "global"

function cleanup
{
	for fault in "${faults[@]}"; do
		log_must zinject -c "$fault"
	done
	poolexists "$TESTPOOL1" && destroy_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$scan_suspend"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$rebuild_scrub"
	log_must set_tunable64 REBUILD_VDEV_LIMIT "$rebuild_limit"
	log_must set_tunable32 VDEV_MIN_MS_COUNT "$ms_count"
	rm -rf "$workdir"
}

log_assert "Rebuild cleanup does not scan after detaching the only stale device"
scan_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
rebuild_scrub=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
rebuild_limit=$(get_tunable REBUILD_VDEV_LIMIT) ||
    log_fail "cannot read REBUILD_VDEV_LIMIT"
ms_count=$(get_tunable VDEV_MIN_MS_COUNT) ||
    log_fail "cannot read VDEV_MIN_MS_COUNT"
workdir=$(mktemp -d "$TEST_BASE_DIR/rebuild_detach.XXXXXX") ||
    log_fail "cannot create test directory"
typeset -a faults
log_onexit cleanup
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must set_tunable64 REBUILD_VDEV_LIMIT $((1024 * 1024))
log_must set_tunable32 VDEV_MIN_MS_COUNT 2
log_must truncate -s 600M "$workdir"/disk{0..2}
log_must zpool create -f -O compression=off -O copies=2 \
    -O primarycache=metadata -m "$workdir/mnt" "$TESTPOOL1" \
    mirror "$workdir"/disk{0,1}
log_must file_write -o create -f "$workdir/mnt/file" \
    -b 1048576 -c 32 -d 65
log_must zpool sync "$TESTPOOL1"
config=$(zdb -C "$TESTPOOL1") || log_fail "cannot read vdev configuration"
top_zap=$(awk '/com.delphix:vdev_zap_top:/ {print $2; exit}' <<< "$config")
ms_shift=$(awk '/metaslab_shift:/ {print $2; exit}' <<< "$config")
asize=$(awk '/asize:/ {print $2; exit}' <<< "$config")
[[ -n "$top_zap" && -n "$ms_shift" && -n "$asize" ]] ||
    log_fail "missing mirror configuration"
(( (asize >> ms_shift) == 2 )) || log_fail "expected two metaslabs"
last_metaslab=$((1 << ms_shift))

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s "$TESTPOOL1" "$workdir/disk0" "$workdir/disk2"
for disk in 0 1; do
	fault=$(zinject -q -d "$workdir/disk$disk" -D 1000:1 \
	    -T read "$TESTPOOL1") || log_fail "cannot delay rebuild reads"
	faults+=("$fault")
done
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
for ((i = 0; i < 90; i++)); do
	progress=$(zdb -dddd "$TESTPOOL1" "$top_zap") ||
	    log_fail "cannot read rebuild progress"
	read -r state offset max_txg <<< "$(awk \
	    '/org.openzfs:vdev_rebuild/ {print $3, $4, $6}' <<< "$progress")"
	[[ "$state" == 2 || "$state" == 3 ]] &&
	    log_fail "rebuild ended before the late writes"
	[[ "$state" == 1 ]] && (( offset >= last_metaslab )) && break
	sleep 1
done
(( i < 90 )) || log_fail "rebuild did not reach the final metaslab"
log_note "Final-metaslab rebuild offset $offset, maximum TXG $max_txg"
fault=$(zinject -q -d "$workdir/disk0" -e io -T write "$TESTPOOL1") ||
    log_fail "cannot make the old child miss writes"
faults+=("$fault")
log_must file_write -o create -f "$workdir/mnt/late" \
    -b 1048576 -c 4 -d 66
log_must zpool sync "$TESTPOOL1"
log_must is_pool_resilvering "$TESTPOOL1"
for ((i = 0; i < 2; i++)); do
	log_must zinject -c "${faults[i]}"
done
faults=("$fault")
hits=$(zinject | awk -v id="$fault" '$1 == id {print $8}')
log_must test "$hits" -gt 0
log_must zpool wait -t replace,resilver "$TESTPOOL1"
status=$(zpool status -P "$TESTPOOL1") || log_fail "cannot read pool status"
log_mustnot grep -Fq "$workdir/disk0" <<< "$status"
for fault in "${faults[@]}"; do
	log_must zinject -c "$fault"
done
unset faults

# Export drains replacement cleanup and syncs any scheduled scan restart.
log_must zpool export "$TESTPOOL1"
history=$(zdb -he -p "$workdir" "$TESTPOOL1") ||
    log_fail "cannot read exported pool history"
log_must grep -q 'rebuild.* complete$' <<< "$history"
if grep -q 'scan setup func=' <<< "$history"; then
	log_note "$history"
	log_fail "automatic scan started after the stale device was detached"
fi

log_pass "Replacement cleanup discards repair demand for the detached device"

#!/bin/ksh -p
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
#

. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/rebuild_test.kshlib

#
# DESCRIPTION:
#	A rebuild retires only the DTLs of the devices it rebuilt, and only
#	when the rebuild itself completed without errors.
#
# STRATEGY:
#	1. Rebuild a clean device beside a writable one whose earlier writes
#	   failed; the rebuild must not retire the other device's DTL.
#	2. After a clean scrub, rebuild while every copy fails its reads. The
#	   rebuild's errors must keep the new device's DTL until healing
#	   completes the replacement.
#	3. Repeat with the recovery override: it may retire the DTL, but the
#	   rebuild must still report its errors.
#

verify_runnable "global"
log_assert "Rebuilds retire only DTLs they rebuilt without errors"
rebuild_test_init

log_note "Testing a writable nonparticipant"
# Give disk-1 a DTL without leaving live data that only disk-0 holds.
rebuild_test_create 2 mirror
rebuild_test_inject -d "$rebuild_dir/disk-1" -e io -T write -f 100
log_must dd if=/dev/urandom of="$rebuild_dir/mnt/lost" bs=1M count=4
sync_pool "$TESTPOOL1"
log_must rebuild_test_clear_faults
log_must rm "$rebuild_dir/mnt/lost"
sync_pool "$TESTPOOL1"
log_must check_state "$TESTPOOL1" "$rebuild_dir/disk-1" online
log_must rebuild_test_missing "$rebuild_dir/disk-1"
log_must zpool replace -s "$TESTPOOL1" "$rebuild_dir/disk-0" "$rebuild_target"
rebuild_test_completed
# Only a healing pass, which the rebuild alone does not start, may retire it.
log_must eval "rebuild_test_missing '$rebuild_dir/disk-1' || rebuild_test_healed"
rebuild_test_finish

for mode in errors override; do
	log_note "Testing failed rebuild reads with $mode"
	rebuild_test_create 2 mirror
	[[ "$mode" = override ]] && log_must set_tunable32 SCAN_IGNORE_ERRORS 1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace -s "$TESTPOOL1" \
	    "$rebuild_dir/disk-0" "$rebuild_target"
	# Fail the new device's reads too, so that no copy can satisfy one.
	rebuild_test_inject -d "$rebuild_dir/disk-0" -e io -T read -f 100
	rebuild_test_inject -d "$rebuild_dir/disk-1" -e io -T read -f 100
	rebuild_test_inject -d "$rebuild_target" -e io -T read -f 100
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	rebuild_test_completed
	log_must rebuild_test_clear_faults
	log_must check_pool_status "$TESTPOOL1" "scan" \
	    "with [1-9][0-9]* errors" true
	if [[ "$mode" = override ]]; then
		log_must rebuild_test_wait replace
		log_must set_tunable32 SCAN_IGNORE_ERRORS 0
		destroy_pool "$TESTPOOL1"
		rebuild_pool_created=0
		continue
	fi
	# The earlier clean scrub must not stand in for the failed rebuild.
	log_must rebuild_test_missing "$rebuild_target"
	rebuild_test_finish
done

log_pass "Rebuilds retired only DTLs they rebuilt without errors"

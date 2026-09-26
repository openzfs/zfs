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
#	when the rebuild itself completed without errors. Failed repair
#	writes and dRAID rows it cannot reconstruct count as errors.
#
# STRATEGY:
#	Exercise failed writes, including failfast failures which are never
#	retried, speculative writes, a new device that is offline during its
#	rebuild, and dRAID rows with too few readable columns. Require nonzero
#	rebuild-local errors with post-rebuild verification disabled, and once
#	more with the default verification scrub, then heal and compare data
#	after export/import.
#	Rebuild a clean device beside a writable one whose earlier writes
#	failed; the rebuild must not retire the other device's DTL. After a
#	clean scrub, rebuild while every copy fails its reads: the new device
#	must keep its DTL until healing completes the replacement, and
#	with the recovery override the rebuild must still report its errors.
#

verify_runnable "global"
log_assert "Rebuilds retire only DTLs they rebuilt without errors"
rebuild_test_init

for mode in mirror mirror_failfast mirror_scrub target_offline draid \
    draid_speculative draid_read_errors; do
	log_note "Testing $mode rebuild outcome"
	case "$mode" in
		mirror|mirror_failfast|mirror_scrub) rebuild_test_create 1 ;;
		target_offline) rebuild_test_create 2 mirror ;;
		draid) rebuild_test_create 5 draid2:3d:5c:0s ;;
		draid_speculative|draid_read_errors)
			rebuild_test_create 3 draid1:2d:3c:0s ;;
	esac

	# With one unavailable child and a full-width dRAID1 group, every row
	# consumes all parity. Repair of the new child is speculative.
	if [[ "$mode" = draid_speculative || "$mode" = draid_read_errors ]]; then
		log_must zpool offline -f "$TESTPOOL1" "$rebuild_dir/disk-1"
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace -s "$TESTPOOL1" \
	    "$rebuild_dir/disk-0" "$rebuild_target"

	case "$mode" in
		target_offline)
			# Writes to a device being rebuilt fail while it is
			# offline. A second new device keeps the rebuild going.
			log_must truncate -s 512M "$rebuild_dir/disk-extra"
			log_must zpool attach -s "$TESTPOOL1" \
			    "$rebuild_dir/disk-1" "$rebuild_dir/disk-extra"
			log_must zpool offline "$TESTPOOL1" "$rebuild_target"
			;;
		mirror_failfast)
			# Failfast errors are not retried, so the first failure
			# of each repair write is final.
			rebuild_test_inject -F -d "$rebuild_target" \
			    -e noop -T write -f 100
			rebuild_test_inject -F -d "$rebuild_target" \
			    -e io -T write -f 100
			;;
		draid_read_errors)
			# The unavailable child plus this error exceed parity.
			rebuild_test_inject -F -d "$rebuild_dir/disk-2" \
			    -e io -T read -f 100
			;;
		mirror_scrub)
			# The scrub that follows cannot tell a silently dropped
			# repair from a successful one, so report every failure.
			rebuild_test_inject -d "$rebuild_target" \
			    -e io -T write -f 100
			;;
		*)
			# Drop data as well as reporting failure.
			rebuild_test_inject -d "$rebuild_target" \
			    -e noop -T write -f 100
			rebuild_test_inject -d "$rebuild_target" \
			    -e io -T write -f 100
			;;
	esac
	[[ "$mode" = mirror_scrub ]] &&
	    log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	rebuild_test_completed '[1-9][0-9]*'
	log_must rebuild_test_wait_fired $rebuild_inject_ids
	if [[ "$mode" = mirror_scrub ]]; then
		# Whichever pass follows the rebuild fails the same repairs
		# and must not retire them.
		log_must rebuild_test_wait scrub
		log_must rebuild_test_wait resilver
		log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
	fi

	log_must rebuild_test_retained
	if [[ "$mode" = target_offline ]]; then
		log_must zpool online "$TESTPOOL1" "$rebuild_target"
	fi
	if [[ "$mode" = mirror* ]]; then
		# This checks the DTL, not just a race with async detachment.
		log_mustnot zpool detach "$TESTPOOL1" "$rebuild_dir/disk-0"
	fi
	if [[ "$mode" = mirror_failfast ]]; then
		# The failfast errors also fail ordinary writes to the new
		# device. Check only the rebuild's own accounting.
		log_must rebuild_test_clear_faults
		destroy_pool "$TESTPOOL1"
		rebuild_pool_created=0
		continue
	fi
	rebuild_test_finish
done

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
rebuild_test_completed 0
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
	rebuild_test_completed '[1-9][0-9]*'
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

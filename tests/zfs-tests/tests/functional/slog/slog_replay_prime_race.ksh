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

#
# Copyright (c) 2026, Amazon.com, Inc. or its affiliates.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify concurrent dataset replay keeps priming state per replay.
#
# STRATEGY:
#	1. Create unequal TX_WRITE logs in two sibling datasets.
#	2. Delay every replay record so both datasets replay concurrently.
#	3. Verify every post-freeze write was replayed.
#	4. Create a one-record replay with parse delay disabled.
#	5. Delay the prime callback and verify replay drains it before return.
#

verify_runnable "global"
tunable_exists ZIL_REPLAY_RECORD_DELAY_MS ||
    log_unsupported "ZIL replay race testing requires a ZFS_DEBUG build"
tunable_exists ZIL_REPLAY_PRIME_DELAY_MS ||
    log_unsupported "ZIL replay drain testing requires a ZFS_DEBUG build"

typeset BIG_DATASET="$TESTPOOL/big"
typeset SMALL_DATASET="$TESTPOOL/small"
typeset DBGMSG_NEW="$TEST_BASE_DIR/dbgmsg.new"
typeset -i BIG_FILE_COUNT=200
typeset -i SMALL_FILE_COUNT=50
typeset -i prime_saved=0
typeset -i delay_saved=0
typeset -i prime_delay_saved=0
typeset -i txg_saved=0
typeset -i dbgmsg_saved=0
typeset -i dbgmsg_lines

function cleanup_prime_race
{
	(( prime_saved != 0 )) &&
	    log_must restore_tunable ZIL_REPLAY_PRIME_ARC
	(( delay_saved != 0 )) &&
	    log_must restore_tunable ZIL_REPLAY_RECORD_DELAY_MS
	(( prime_delay_saved != 0 )) &&
	    log_must restore_tunable ZIL_REPLAY_PRIME_DELAY_MS
	(( txg_saved != 0 )) && log_must restore_tunable TXG_TIMEOUT
	(( dbgmsg_saved != 0 )) && log_must restore_tunable DBGMSG_MAXSIZE
	rm -f "$DBGMSG_NEW"
	cleanup
}

function write_patterns
{
	typeset dataset="$1"
	typeset -i count="$2"
	typeset prefix="$3"
	typeset file pattern
	typeset -i i=1

	while (( i <= count )); do
		file="/$dataset/file.$i"
		pattern=$(printf "%s-%04d" "$prefix" "$i")
		log_must eval "printf '%s' '$pattern' | \
		    dd of='$file' conv=notrunc,fdatasync status=none"
		(( i = i + 1 ))
	done
}

#
# Save the debug messages logged after the first dbgmsg_lines lines in
# DBGMSG_NEW. FreeBSD can't clear the log, so the test skips the old lines
# instead, and raises DBGMSG_MAXSIZE so that none are dropped while it runs.
#
function save_new_dbgmsgs
{
	dbgmsg_read | tail -n +$(( dbgmsg_lines + 1 )) > "$DBGMSG_NEW" ||
	    log_fail "Couldn't read the debug message log"
}

function verify_patterns
{
	typeset dataset="$1"
	typeset -i count="$2"
	typeset prefix="$3"
	typeset file pattern actual
	typeset -i i=1

	while (( i <= count )); do
		file="/$dataset/file.$i"
		pattern=$(printf "%s-%04d" "$prefix" "$i")
		log_must test -f "$file"
		actual=$(cat "$file")
		[[ "$actual" == "$pattern" ]] || log_fail \
		    "$file contains '$actual', expected '$pattern'"
		(( i = i + 1 ))
	done
}

log_assert "ZIL replay isolates priming state and drains prime callbacks"
log_onexit cleanup_prime_race

log_must setup
log_must save_tunable ZIL_REPLAY_PRIME_ARC
prime_saved=1
log_must save_tunable ZIL_REPLAY_RECORD_DELAY_MS
delay_saved=1
log_must save_tunable ZIL_REPLAY_PRIME_DELAY_MS
prime_delay_saved=1
log_must save_tunable TXG_TIMEOUT
txg_saved=1
log_must save_tunable DBGMSG_MAXSIZE
dbgmsg_saved=1

log_must set_tunable32 ZIL_REPLAY_PRIME_ARC 1
log_must set_tunable32 ZIL_REPLAY_RECORD_DELAY_MS 20
log_must set_tunable32 ZIL_REPLAY_PRIME_DELAY_MS 0
log_must set_tunable32 TXG_TIMEOUT 1
log_must set_tunable32 DBGMSG_MAXSIZE $((1 << 28))

log_must zpool create "$TESTPOOL" $VDEV
log_must zfs create "$BIG_DATASET"
log_must zfs create "$SMALL_DATASET"

# Create every target before the freeze and force each dataset's ZIL header
# to disk. The post-freeze workload then contains TX_WRITE records only.
write_patterns "$BIG_DATASET" "$BIG_FILE_COUNT" before
write_patterns "$SMALL_DATASET" "$SMALL_FILE_COUNT" before
log_must sync
log_must zpool sync "$TESTPOOL"

log_must zpool freeze "$TESTPOOL"

# Both prefixes have the same length, so these overwrites do not grow files.
write_patterns "$BIG_DATASET" "$BIG_FILE_COUNT" after_
write_patterns "$SMALL_DATASET" "$SMALL_FILE_COUNT" after_

log_note "Verify both datasets have TX_WRITE records to replay"
log_must eval "zdb -iv '$BIG_DATASET' 2>&1 | grep -q TX_WRITE"
log_must eval "zdb -iv '$SMALL_DATASET' 2>&1 | grep -q TX_WRITE"

log_must zfs unmount "$BIG_DATASET"
log_must zfs unmount "$SMALL_DATASET"
log_must zpool export "$TESTPOOL"

typeset -i dmesg_lines
dmesg_lines=$(dmesg | wc -l)
dbgmsg_lines=$(dbgmsg_read | wc -l)
log_must zpool import -f -d "$VDIR" "$TESTPOOL"
save_new_dbgmsgs

typeset -i priming_peak
priming_peak=$(awk '
    /Starting zfs_zil_replay_prime_arc/ {
        active++
        if (active > peak)
            peak = active
    }
    /Finished zfs_zil_replay_prime_arc/ { active-- }
    END { print peak + 0 }
' "$DBGMSG_NEW")
(( priming_peak == 2 )) || log_fail \
    "ARC priming peak was $priming_peak concurrent replays, expected 2"

verify_patterns "$BIG_DATASET" "$BIG_FILE_COUNT" after_
verify_patterns "$SMALL_DATASET" "$SMALL_FILE_COUNT" after_

log_note "Verify replay drains an outstanding priming callback"
log_must zpool destroy "$TESTPOOL"
log_must set_tunable32 ZIL_REPLAY_RECORD_DELAY_MS 0
log_must set_tunable32 ZIL_REPLAY_PRIME_DELAY_MS 5000

log_must zpool create "$TESTPOOL" $VDEV
log_must zfs create "$BIG_DATASET"

write_patterns "$BIG_DATASET" 1 before
log_must sync
log_must zpool sync "$TESTPOOL"
log_must zpool freeze "$TESTPOOL"
write_patterns "$BIG_DATASET" 1 after_

log_note "Verify the dataset has a TX_WRITE record to replay"
log_must eval "zdb -iv '$BIG_DATASET' 2>&1 | grep -q TX_WRITE"

log_must zfs unmount "$BIG_DATASET"
log_must zpool export "$TESTPOOL"
dbgmsg_lines=$(dbgmsg_read | wc -l)
log_must zpool import -f -d "$VDIR" "$TESTPOOL"
save_new_dbgmsgs

log_must grep -Eq \
    "Waiting for ZIL replay prime tasks" \
    "$DBGMSG_NEW"
verify_patterns "$BIG_DATASET" 1 after_

typeset dmesg_new="$TEST_BASE_DIR/dmesg.new"
log_must eval "dmesg | tail -n +$(( dmesg_lines + 1 )) > '$dmesg_new'"
log_mustnot grep -q "ZFS replay transaction error" "$dmesg_new"

log_pass "ZIL replay isolated priming state and drained prime callbacks"

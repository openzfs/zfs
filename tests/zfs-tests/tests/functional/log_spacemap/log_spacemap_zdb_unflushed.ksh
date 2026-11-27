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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# zdb opens every pool read-only, and a pool that is not writeable does
# not read the unflushed changes of its log space maps: zdb -mmm loads
# each metaslab from its own space map alone.  The weight of an unloaded
# metaslab comes from its space map histogram, which counts the free
# segments of the unflushed changes too.  When unflushed frees make
# larger free segments than the metaslab's own space map has, the loaded
# weight is lower than the unloaded one, which a debug zdb must accept.
#
# STRATEGY:
#	1. Create a pool of 32M metaslabs and fill a dozen of them with
#	   the 1M blocks of one file.
#	2. Rewrite every eighth block, which leaves a 1M hole where each
#	   of them was, and export and import the pool, which writes all
#	   changes to the metaslabs' own space maps: they show only those
#	   holes as free.
#	3. Remove the file.  Its frees, which merge into segments of
#	   several megabytes, stay in the log space maps but reach the
#	   histograms.
#	4. Take a checkpoint, keep the log space maps at export and export
#	   the pool.
#	5. Verify that zdb loads every metaslab, in the current and in the
#	   checkpointed state, and that the weight of some of them was
#	   lower once loaded.
#

verify_runnable "global"

LOGSM_POOL="logsm_zdb_unflushed"
VDEV_DIR="$TEST_BASE_DIR/logsm_zdb_unflushed"
FILE="/$LOGSM_POOL/file"
BLOCKS=384

function cleanup
{
	restore_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
	if poolexists $LOGSM_POOL; then
		log_must zpool destroy -f $LOGSM_POOL
	fi
	rm -rf $VDEV_DIR
}
log_onexit cleanup

#
# Print the metaslabs of a zdb -mmm output that have free segments once
# loaded, but whose on-disk histogram goes up to a larger bucket than
# those segments: the metaslabs whose weight drops when they are loaded.
#
function weight_drops # <zdb output>
{
	awk '
	function done() {
		if (ms != "" && mem >= 0 && disk > mem)
			print "metaslab " ms ": on-disk histogram up to 2^" \
			    disk ", loaded segments up to 2^" mem
	}
	/^[ \t]+metaslab +[0-9]+ +offset / {
		done(); ms = $2; mem = -1; disk = -1; hist = ""; next
	}
	/In-memory histogram:/ { hist = "mem"; next }
	/On-disk histogram:/ { hist = "disk"; next }
	hist != "" && $1 ~ /^[0-9]+:$/ {
		if (hist == "mem")
			mem = $1 + 0
		else
			disk = $1 + 0
		next
	}
	{ hist = "" }
	END { done() }' "$1"
}

#
# Run zdb on the exported pool, without a core dump, and fail with its
# assertion, or the end of its error output, if it does not complete.
#
function zdb_load # <output> <zdb options>
{
	typeset out=$1
	shift

	(ulimit -c 0; zdb -e -p $VDEV_DIR "$@" $LOGSM_POOL) > $out 2> $out.err
	typeset -i rc=$?
	if ((rc != 0)); then
		tr '\r' '\n' < $out.err > $out.msg
		typeset msg=$(grep -E 'ASSERT|VERIFY' $out.msg | head -n 2)
		[[ -n $msg ]] || msg=$(tail -n 2 $out.msg)
		echo "$msg" | while read -r line; do
			log_note "zdb $*: $line"
		done
		log_fail "zdb $* exited $rc"
	fi
}

log_assert "zdb loads metaslabs whose unflushed frees merged free space"

log_must save_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT

log_must mkdir -p $VDEV_DIR
log_must truncate -s 1G $VDEV_DIR/vdev
log_must zpool create -f -o cachefile=none -O compression=off \
    -O recordsize=1M $LOGSM_POOL $VDEV_DIR/vdev

log_must dd if=/dev/urandom of=$FILE bs=1M count=$BLOCKS
sync_pool $LOGSM_POOL
typeset -i b
for ((b = 4; b < BLOCKS; b += 8)); do
	log_must dd if=/dev/urandom of=$FILE bs=1M count=1 seek=$b \
	    conv=notrunc
done
sync_pool $LOGSM_POOL
log_must zpool export $LOGSM_POOL
log_must zpool import -o cachefile=none -d $VDEV_DIR $LOGSM_POOL

typeset -i target=$(get_pool_prop allocated $LOGSM_POOL)
((target -= BLOCKS * 1024 * 1024 / 2))
log_must rm $FILE
typeset -i i=0
while (($(get_pool_prop allocated $LOGSM_POOL) > target)); do
	((i++ < 60)) || log_fail "the frees of $FILE did not complete"
	sync_pool $LOGSM_POOL
	sleep 1
done

log_must zpool checkpoint $LOGSM_POOL
log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must zpool export $LOGSM_POOL
log_must eval "zdb -e -p $VDEV_DIR -m $LOGSM_POOL | grep -q 'Log Spacemap'"

zdb_load $VDEV_DIR/current -mmm
zdb_load $VDEV_DIR/checkpointed -k -mmmm

for state in current checkpointed; do
	typeset drops=$(weight_drops $VDEV_DIR/$state)
	[[ -n $drops ]] || log_fail \
	    "no metaslab weight dropped in the $state state, nothing was tested"
	echo "$drops" | while read -r line; do
		log_note "$state: $line"
	done
done

log_pass "zdb loads metaslabs whose unflushed frees merged free space"

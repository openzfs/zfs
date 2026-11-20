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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A block that syncing context must read, but that reads without an I/O
# error and then cannot be decompressed, suspends the pool instead of
# crashing the host, and "zpool clear" resumes it (#19856).  The block is
# the last block of a metaslab space map, read to append to the map when
# the metaslab is flushed.  With the compressed ARC, a decompression error
# injected into it fails in the ARC, after the read.
#
# STRATEGY:
# 1. Create a pool of two file vdevs, write data, free some of it, and stop
#    allocations from the second vdev, so that its metaslabs stay unloaded.
# 2. Export and import the pool, keeping the log space maps: the space maps
#    of the second vdev are no longer cached.
# 3. Inject decompression errors into the last block of every space map of
#    the second vdev.
# 4. Free the rest of the data, and write and sync until an injection
#    fires.
# 5. Verify that the pool suspends.
# 6. Clear the injections and the pool, and verify that the pool syncs, is
#    healthy and leaked no space.
#
# Every command that a regression could leave blocked in the kernel runs
# in the background, with its output in a file, and is waited for with a
# timeout: the test runner, after killing a test, waits for every process
# that holds its output pipes, so one blocked process would stall the run.
#

verify_runnable "global"

typeset VDEV_DIR=$TEST_BASE_DIR/zinject_decompress_spacemap
typeset VDEV0=$VDEV_DIR/vdev0
typeset VDEV1=$VDEV_DIR/vdev1
typeset OUT=$TEST_BASE_DIR/zinject_decompress_spacemap.out
set -A bg_pids

function cleanup
{
	bounded 60 zinject -c all
	# A pool still suspended can be neither cleared nor destroyed: leave
	# it rather than block the rest of the run.
	if bounded 60 poolexists $TESTPOOL &&
	    bounded 60 zpool clear $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
	rm -rf $VDEV_DIR $OUT
	restore_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
	restore_tunable METASLAB_PRELOAD_ENABLED
}

# Total of the injection counts of the records on the pool
function injections
{
	zinject | awk -v pool=$TESTPOOL '
	    $1 ~ /^[0-9]+$/ && $2 == pool { n += $NF }
	    END { print n + 0 }'
}

# Print where the given processes, the background commands, their
# children and the sync threads wait.  On Linux, also dump the blocked
# tasks to the kernel log, which is printed when the test fails.
function print_stacks
{
	typeset pid
	typeset -a pids=("$@" "${bg_pids[@]}")

	for pid in "${pids[@]}"; do
		pids+=($(pgrep -P $pid))
	done
	pids+=($(pgrep -x txg_sync))
	if is_linux && [[ -w /proc/sysrq-trigger ]]; then
		echo w > /proc/sysrq-trigger
	fi
	for pid in "${pids[@]}"; do
		kill -0 $pid 2>/dev/null || continue
		ps -o pid=,wchan=,args= -p $pid
		is_linux && cat /proc/$pid/stack
	done
}

# Run a command for at most $1 seconds, and return its status
function bounded
{
	typeset -i timeout=$1
	shift

	"$@" > $OUT 2>&1 &
	typeset pid=$!
	while kill -0 $pid 2>/dev/null; do
		if ((timeout-- == 0)); then
			cat $OUT
			print_stacks $pid
			echo "$* did not return" >&2
			return 1
		fi
		sleep 1
	done
	wait $pid
	typeset -i status=$?
	cat $OUT
	return $status
}

# Wait up to $1 seconds for the commands started in the background
function wait_bg
{
	typeset -i timeout=$1
	typeset pid

	for pid in "${bg_pids[@]}"; do
		while kill -0 $pid 2>/dev/null; do
			if ((timeout-- == 0)); then
				print_stacks
				return 1
			fi
			sleep 1
		done
	done
	return 0
}

log_onexit cleanup

log_assert "A space map block that fails to decompress in syncing context" \
    "suspends the pool, and the pool resumes"

log_must save_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
log_must set_tunable32 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must save_tunable METASLAB_PRELOAD_ENABLED
log_must set_tunable32 METASLAB_PRELOAD_ENABLED 0

log_must mkdir -p $VDEV_DIR
log_must truncate -s $MINVDEVSIZE $VDEV0 $VDEV1
log_must zpool create -f $TESTPOOL $VDEV0 $VDEV1

# Each txg that dirties the MOS flushes at least one metaslab, so that the
# space maps of the metaslabs written to are no longer empty.
typeset -i i
for ((i = 0; i < 16; i++)); do
	log_must file_write -o create -f /$TESTPOOL/file.$i -b 1048576 -c 8 \
	    -d R
	log_must zpool sync $TESTPOOL
done
for ((i = 0; i < 4; i++)); do
	log_must rm /$TESTPOOL/file.$i
	log_must zpool sync $TESTPOOL
done
log_must zpool set allocating=off $TESTPOOL $VDEV1
log_must zpool export $TESTPOOL
log_must zpool import -d $VDEV_DIR $TESTPOOL
# The import requests a config update, made after the next txg while holding
# spa_namespace_lock.  Get it done: if the pool suspended under it, "zinject"
# and "zpool clear" would wait for that lock forever.
log_must zpool sync $TESTPOOL

# A flush appends to the last block of the space map.  Under 4K, that is
# block 0, whatever the block size of the map.
zdb -m $TESTPOOL | awk '
    $1 == "vdev" { vdev = $2; obj = 0 }
    $1 == "metaslab" && $5 == "spacemap" { obj = $6 }
    /^Log/ { vdev = ""; obj = 0 }
    $1 == "smp_length" && vdev == 1 && obj != 0 { print obj, $3; obj = 0 }
    ' > $VDEV_DIR/maps
typeset -i armed=0
typeset obj len
while read -r obj len; do
	((len > 0 && len < 4096)) || continue
	log_must zinject -b "0:$(printf "%x" $obj):0:0" -e decompress $TESTPOOL
	((armed += 1))
done < $VDEV_DIR/maps
((armed > 0)) || log_fail "no space map to inject into: $(<$VDEV_DIR/maps)"

# A write dirties the MOS in its txg, so that the txg flushes at least the
# metaslab flushed least recently.  "zpool sync -f" would too, but it waits
# for the sync in progress as SCL_CONFIG writer, and "zpool clear" queues
# behind it: if that sync suspends, the pool cannot be resumed.  Once the
# pool is suspended, "rm", the writes and "zpool sync" wait for "zpool
# clear": run them in the background.
rm -f /$TESTPOOL/file.* > /dev/null 2>&1 &
bg_pids+=($!)
typeset -i tries=60
until (($(injections) > 0)); do
	if ((tries-- == 0)); then
		zinject
		log_fail "no injection fired"
	fi
	(echo $tries > /$TESTPOOL/dirty && zpool sync $TESTPOOL) \
	    > /dev/null 2>&1 &
	bg_pids+=($!)
	sleep 1
done

tries=30
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; do
	if ((tries-- == 0)); then
		log_fail "pool did not suspend"
	fi
	sleep 1
done

log_must bounded 60 zinject -c all
log_must bounded 60 zpool clear $TESTPOOL
wait_bg 60 || log_fail "pool did not sync after zpool clear"
log_must bounded 60 zpool sync $TESTPOOL
log_must test "$(kstat_pool $TESTPOOL state)" = "ONLINE"
log_must bounded 60 zpool status -x $TESTPOOL
log_must grep -q 'is healthy' $OUT

log_must bounded 60 zpool export $TESTPOOL
log_must zdb -bcc -e -p $VDEV_DIR $TESTPOOL

log_pass "A space map block that fails to decompress in syncing context" \
    "suspends the pool, and the pool resumes"

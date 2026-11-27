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
#	Reading blocks whose first copy fails must not hang a RAIDZ
#	expansion.
#
#	When one DVA of a block with several copies fails,
#	vdev_mirror_io_done() reads the next copy from the read interrupt
#	thread that completed the failed one.  During an expansion, a raidz
#	read first takes the expansion rangelock for its range, and the
#	reflow holds the range it copies as a writer until that copy's
#	reads (completed by the same interrupt threads) and writes are done.
#	A retry that waited for the lock in the interrupt thread kept the
#	reflow's reads from ever completing when it was the only such
#	thread (as on a machine with one or two CPUs): the reflow, txg sync
#	and every later I/O to the pool hung.
#
# STRATEGY:
#	1. Use a single read interrupt thread, and create a raidz1 pool
#	   with copies=2 and primarycache=metadata, so that every read goes
#	   to disk and every block has two DVAs on the raidz vdev
#	2. Write files, record their checksums, and pause the expansion
#	   halfway with raidz_expand_max_reflow_bytes
#	3. Slow the reads from one child, so that the reflow keeps copies in
#	   flight, and let the expansion run
#	4. Fail the first DVA of every block and read all the files at once,
#	   a few times while the expansion runs
#	5. Verify that the reads finish with the right data, that the first
#	   DVAs failed and the expansion went on meanwhile, then that the
#	   expansion completes and the pool is healthy
#

typeset -r devs=5
typeset -r dev_size_mb=256
typeset -r nfiles=16
typeset -r file_mb=12
typeset -r read_timeout=600
typeset -r passes=4

typeset -a disks
typeset -A sums

original_taskq_read=$(get_tunable ZIO_TASKQ_READ)
original_scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)

function cleanup
{
	log_must zinject -c all
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0

	poolexists "$TESTPOOL" && zpool status -v "$TESTPOOL"
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {1..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
	for i in {1..$nfiles}; do
		log_must rm -f "$TEST_BASE_DIR/sum-$i"
	done
	log_must rm -f "$TEST_BASE_DIR/data"

	log_must set_tunable32 SCRUB_AFTER_EXPAND $original_scrub_after_expand
	log_must set_tunable_string ZIO_TASKQ_READ "$original_taskq_read"
}

function expanding # pool
{
	zpool status "$1" | grep -q 'expansion of .* in progress'
}

function reflowed # pool
{
	zpool status -jp --json-int "$1" | \
	    jq --arg pool "$1" '.pools[$pool].raidz_expand_stats.reflowed'
}

# set_tunable does not quote the value for sysctl, and the Linux parser of
# zio_taskq_read rejects the newline echo would add.
function set_tunable_string # tunable value
{
	eval "typeset tunable=\$$1"
	if is_linux; then
		printf "%s" "$2" > /sys/module/zfs/parameters/$tunable
	else
		sysctl vfs.zfs.$tunable="$2"
	fi
}

log_onexit cleanup

log_assert "Reads that retry another DVA do not hang a raidz expansion"

# One read interrupt thread, as on a machine with one or two CPUs.  This
# applies to pools created from now on.
log_must set_tunable_string ZIO_TASKQ_READ 'fixed,1,8 null fixed,1,1 null'
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

for i in {1..$devs}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M ${disks[$i]}
done

log_must zpool create -f -o cachefile=none -O copies=2 \
    -O primarycache=metadata $TESTPOOL raidz1 ${disks[1..$((devs - 1))]}
for i in {1..$nfiles}; do
	# Checksum the data before it goes in: reading it back from the pool
	# would leave it in the ARC.
	log_must dd if=/dev/urandom of=$TEST_BASE_DIR/data bs=1M \
	    count=$file_mb status=none
	sums[$i]=$(xxh128digest $TEST_BASE_DIR/data)
	[[ -n ${sums[$i]} ]] || log_fail "no checksum for file-$i"
	log_must cp $TEST_BASE_DIR/data /$TESTPOOL/file-$i
done
log_must rm -f $TEST_BASE_DIR/data
log_must zpool sync $TESTPOOL

# Pause halfway: from there, the reflow may run well ahead of the progress
# it has synced, so it holds a large range while its reads are slowed.
typeset -i half=$(($(get_pool_prop allocated $TESTPOOL) / 2))
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $half
log_must zpool attach $TESTPOOL raidz1-0 ${disks[$devs]}
wait_raidz_expand_paused $TESTPOOL
before=$(reflowed $TESTPOOL)
[[ $before == +([0-9]) ]] || log_fail "reflowed '$before'"
log_note "paused with $before bytes reflowed"

log_must zinject -d ${disks[2]} -D 1000:64 -T read $TESTPOOL
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
sleep 3

for i in {1..$nfiles}; do
	log_must zinject -t data -e io -C 0 /$TESTPOOL/file-$i
done

# Whether a retry lands in a range being copied depends on the layout, so
# read everything again while the expansion runs.
typeset -a pids
typeset -i pass
typeset -i injected=1
for pass in {1..$passes}; do
	expanding $TESTPOOL || break

	# primarycache=metadata buffers are evicted lazily: flush the ARC
	# (zinject -a), so that every pass reads from the disks.
	log_must zinject -a -t data -e io -C 0 /$TESTPOOL/file-1
	for i in {1..$nfiles}; do
		xxh128digest /$TESTPOOL/file-$i > $TEST_BASE_DIR/sum-$i &
		pids[$i]=$!
	done

	# With the deadlock the reads never finish, and neither does the
	# expansion.  Once the expansion is done, reads no longer take its
	# lock and cannot hang this way: clear the injections, so that the
	# rest of the pass does not wait for the slowed child.
	typeset -i t0=$SECONDS
	for i in {1..$nfiles}; do
		while kill -0 ${pids[$i]} 2>/dev/null; do
			if ((SECONDS - t0 > read_timeout)); then
				zpool status -v $TESTPOOL
				log_fail "pass $pass: reads of the files did" \
				    "not finish in $read_timeout seconds"
			fi
			if ((injected)) && ! expanding $TESTPOOL; then
				log_must zinject -c all
				injected=0
			fi
			sleep 1
		done
		wait ${pids[$i]} || \
		    log_fail "pass $pass: reading file-$i failed"
	done
	log_note "pass $pass: reads finished in $((SECONDS - t0)) seconds," \
	    "$(reflowed $TESTPOOL) bytes reflowed"

	# Every read retried the second DVA and got the right data.
	for i in {1..$nfiles}; do
		typeset sum=$(<$TEST_BASE_DIR/sum-$i)
		log_must rm -f $TEST_BASE_DIR/sum-$i
		[[ $sum == ${sums[$i]} ]] || log_fail "pass $pass: file-$i:" \
		    "checksum $sum, expected ${sums[$i]}"
	done
done

# The first DVAs did fail, and the expansion went on during the reads.
typeset -i errors=$(zpool status -jp --json-int $TESTPOOL | \
    jq '.pools[].vdevs[].vdevs[] | select(.name == "raidz1-0") |
    .read_errors')
log_note "read errors on raidz1-0: $errors"
((errors > 0)) || log_fail "no read of a first DVA failed"
after=$(reflowed $TESTPOOL)
log_note "$after bytes reflowed after the reads"
((after > before)) || log_fail "the expansion made no progress"

log_must zinject -c all
log_must zpool clear $TESTPOOL
log_must zpool wait -t raidz_expand $TESTPOOL
log_must zpool scrub -w $TESTPOOL
verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
for i in {1..$nfiles}; do
	typeset sum=$(xxh128digest /$TESTPOOL/file-$i)
	[[ $sum == ${sums[$i]} ]] || \
	    log_fail "file-$i after the expansion: checksum $sum"
done

log_pass "Reads that retry another DVA do not hang a raidz expansion"

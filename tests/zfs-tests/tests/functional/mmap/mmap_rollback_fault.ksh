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
#	A rollback or a forced receive into a mounted filesystem must not
#	deadlock with processes which fault in pages of its files.
#
# STRATEGY:
#	1. Create a file, snapshot it and send it to a new filesystem.
#	2. Dirty a page of the file through a mapping, roll back, and verify
#	   that the dirty page is not written back afterwards.
#	3. Start readers which keep faulting random pages of the file in
#	   through mappings, a writer which keeps dirtying pages of another
#	   file, and drop the page cache over and over so that they keep
#	   faulting.
#	4. Roll the source filesystem back to its snapshot repeatedly.
#	5. Do repeated "zfs receive -F" into the still mounted destination.
#	6. Verify that every operation completes in time and that read() and
#	   mmap() return the expected contents afterwards.
#
#	zfs_rezget() drops the cached pages of every file while the filesystem
#	is suspended.  A reader that is faulting a page in holds that page's
#	lock until the filesystem is resumed, so waiting for the page lock
#	there deadlocks the rollback or receive with the reader.  Pages which
#	are not dropped for that reason must not be dirty ones, which would
#	be written back to the rolled back file.
#

verify_runnable "global"

typeset src=$TESTPOOL/$TESTFS
typeset dst=$TESTPOOL/mmap_rollback_fault
typeset srcfile=$TESTDIR/mmap_rollback_fault
typeset dstfile
typeset stream=$TEST_BASE_DIR/mmap_rollback_fault
typeset -i nreaders=4
typeset -i nops=20
typeset -i timeout=60
typeset readers=""
typeset dropper=""
typeset syncer=""

function stop_load
{
	typeset pid

	for pid in $readers $dropper $syncer; do
		kill $pid 2>/dev/null
	done
	for pid in $readers $dropper $syncer; do
		wait $pid 2>/dev/null
	done
	readers=""
	dropper=""
	syncer=""
}

#
# The readers run until they are killed.  If one is gone, the test did not
# exercise anything.
#
function check_readers
{
	typeset pid

	for pid in $readers; do
		kill -0 $pid 2>/dev/null || log_fail "a reader exited early"
	done
}

function cleanup
{
	stop_load
	datasetexists $dst && destroy_dataset $dst "-r"
	datasetexists $src@a && destroy_dataset $src@a
	datasetexists $src@b && destroy_dataset $src@b
	datasetexists $src@c && destroy_dataset $src@c
	rm -f $srcfile $srcfile.dirty $stream.b $stream.c
}

#
# Run a command, reading its input from the file given as first argument, in
# the background and fail if it does not complete within $timeout seconds,
# showing where it is stuck.
#
function run_bounded
{
	typeset input=$1
	typeset pid stuck
	typeset -i i

	shift
	"$@" < $input &
	pid=$!
	for ((i = 0; i < timeout * 10; i++)); do
		if ! kill -0 $pid 2>/dev/null; then
			wait $pid
			return $?
		fi
		sleep 0.1
	done

	log_note "'$*' did not complete within $timeout seconds"
	cat /proc/$pid/stack
	for stuck in $(ps -eo pid,stat | awk '$2 ~ /^D/ { print $1 }'); do
		echo "== $stuck $(cat /proc/$stuck/comm)"
		cat /proc/$stuck/stack
	done
	return 124
}

function start_load
{
	typeset file=$1
	typeset -i i

	for ((i = 0; i < nreaders; i++)); do
		mmap_stale_check fault $file &
		readers="$readers $!"
	done
	mmap_stale_check dirty $file.dirty &
	readers="$readers $!"
	(while true; do
		sync -f $file.dirty
		sleep 0.02
	done) &
	syncer=$!
	(while true; do
		echo 1 > /proc/sys/vm/drop_caches
		sleep 0.05
	done) &
	dropper=$!
}

function modify
{
	typeset file=$1

	log_must dd if=/dev/urandom of=$file bs=4k count=1 seek=7 \
	    conv=notrunc,fsync
}

function check_contents
{
	typeset file=$1
	typeset expect=$2
	typeset got

	got=$(mmap_stale_check csum $file)
	[[ $got == $expect ]] ||
	    log_fail "read() returned stale data ($got != $expect)"
	got=$(mmap_stale_check msum $file)
	[[ $got == $expect ]] ||
	    log_fail "mmap() returned stale data ($got != $expect)"
}

#
# A page which is dirty when the filesystem is rolled back must not be written
# back to the rolled back file.
#
function check_dirty_dropped
{
	typeset file=$1
	typeset expect=$2
	typeset pid
	typeset -i i

	mmap_stale_check poke $file &
	pid=$!
	for ((i = 0; i < 100; i++)); do
		[[ $(mmap_stale_check csum $file) != $expect ]] && break
		sleep 0.1
	done
	if [[ $(mmap_stale_check csum $file) == $expect ]]; then
		kill $pid
		wait $pid 2>/dev/null
		log_fail "the page was not dirtied"
	fi

	log_must run_bounded /dev/null zfs rollback $src@a
	kill $pid
	wait $pid 2>/dev/null
	log_must sync
	echo 3 > /proc/sys/vm/drop_caches
	check_contents $file $expect
}

log_assert "A rollback or receive must not deadlock with page faults"
log_onexit cleanup

[[ -w /proc/sys/vm/drop_caches ]] ||
    log_unsupported "Cannot write to /proc/sys/vm/drop_caches"

typeset -i badpages=$(dmesg | grep -c "Bad page cache")
log_must dd if=/dev/urandom of=$srcfile bs=1M count=8
log_must dd if=/dev/urandom of=$srcfile.dirty bs=1M count=8
log_must zfs snapshot $src@a
typeset expect_a=$(mmap_stale_check csum $srcfile)

# Two different incremental streams on top of @a, for the receive part.
modify $srcfile
log_must zfs snapshot $src@b
typeset expect_b=$(mmap_stale_check csum $srcfile)
log_must eval "zfs send -i $src@a $src@b > $stream.b"
log_must zfs rollback -r $src@a
modify $srcfile
log_must zfs snapshot $src@c
typeset expect_c=$(mmap_stale_check csum $srcfile)
log_must eval "zfs send -i $src@a $src@c > $stream.c"
log_must zfs rollback -r $src@a
[[ $expect_a != $expect_b && $expect_a != $expect_c &&
    $expect_b != $expect_c ]] ||
    log_fail "modifying the file did not change its contents"

log_must eval "zfs send $src@a | zfs receive $dst"
log_must ismounted $dst
dstfile=$(get_prop mountpoint $dst)/mmap_rollback_fault

typeset -i i
typeset snap expect

check_dirty_dropped $srcfile $expect_a

start_load $srcfile
for ((i = 0; i < nops; i++)); do
	modify $srcfile
	log_must run_bounded /dev/null zfs rollback $src@a
done
check_readers
stop_load
check_contents $srcfile $expect_a

start_load $dstfile
for ((i = 0; i < nops; i++)); do
	if ((i % 2 == 0)); then
		snap=b
		expect=$expect_b
	else
		snap=c
		expect=$expect_c
	fi
	datasetexists $dst@b && destroy_dataset $dst@b
	datasetexists $dst@c && destroy_dataset $dst@c
	log_must run_bounded $stream.$snap zfs receive -F $dst
done
check_readers
stop_load
check_contents $dstfile $expect

(( $(dmesg | grep -c "Bad page cache") == badpages )) ||
    log_fail "the page cache was corrupted"

log_pass "A rollback or receive does not deadlock with page faults"

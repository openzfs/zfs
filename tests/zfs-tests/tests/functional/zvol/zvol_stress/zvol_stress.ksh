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
# Copyright (c) 2022 by Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
# Stress test multithreaded transfers to multiple zvols.  Also verify
# zvol errors show up in zpool status.
#
# STRATEGY:
#
# For both the normal submit_bio() codepath and the blk-mq codepath, do
# the following:
#
# 1. Create one zvol per CPU
# 2. In parallel, spawn an fio "write and verify" for each zvol
# 3. Inject write errors
# 4. Write to one of the zvols with dd and verify the errors
#

verify_runnable "global"

num_zvols=$(get_num_cpus)

# If we were making one big zvol from all the pool space, it would
# be this big:
biggest_zvol_size_possible=$(largest_volsize_from_pool $TESTPOOL)

# Crude calculation: take the biggest zvol size we could possibly
# create, knock 10% off it (for overhead) and divide by the number
# of ZVOLs we want to make.
#
# Round the value using a printf
typeset -f each_zvol_size=$(( floor($biggest_zvol_size_possible * 0.9 / \
	$num_zvols )))

typeset tmpdir="$(mktemp -d "$TEST_BASE_DIR/zvol_stress_fio_state.XXXXXX")"

log_must save_tunable VOL_USE_BLK_MQ
log_must save_tunable VOL_REQUEST_SYNC

function create_zvols
{
	log_note "Creating $num_zvols zvols that are ${each_zvol_size}B each"
	for i in $(seq $num_zvols) ; do
		log_must zfs create -V $each_zvol_size $TESTPOOL/testvol$i
		block_device_wait "$ZVOL_DEVDIR/$TESTPOOL/testvol$i"
	done
}

function destroy_zvols
{
	for i in $(seq $num_zvols) ; do
		log_must_busy zfs destroy $TESTPOOL/testvol$i
	done
}

function do_zvol_stress
{
	# Write 10% of each zvol, or 50MB, whichever is less
	zvol_write_size=$((each_zvol_size / 10))
	if [ $zvol_write_size -gt $((50 * 1048576)) ] ; then
		zvol_write_size=$((50 * 1048576))
	fi
	zvol_write_size_mb=$(($zvol_write_size / 1048576))

	if is_linux ; then
		engine=libaio
	else
		engine=psync
	fi

	# Spawn off one fio per zvol in parallel
	pids=""
	for i in $(seq $num_zvols) ; do
		# Spawn one fio per zvol as its own process
		fio --ioengine=$engine --name=zvol_stress$i --direct=0 \
			--filename="$ZVOL_DEVDIR/$TESTPOOL/testvol$i" --bs=1048576 \
			--iodepth=10 --readwrite=randwrite --size=${zvol_write_size} \
			--verify_async=2 --numjobs=1 --verify=sha1 \
			--verify_fatal=1 \
			--continue_on_error=none \
			--error_dump=1 \
			--exitall_on_error \
			--aux-path="$tmpdir" --do_verify=1 &
		pids="$pids $!"
	done

	# Wait for all the spawned fios to finish and look for errors
	fail=""
	i=0
	for pid in $pids ; do
		log_note "$s waiting on $pid"
		if ! wait $pid ; then
			log_fail "fio error on $TESTPOOL/testvol$i"
		fi
		i=$(($i + 1))
	done
}

function cleanup
{
	log_must zinject -c all
	log_must zpool clear $TESTPOOL
	destroy_zvols
	log_must restore_tunable VOL_USE_BLK_MQ
	log_must restore_tunable VOL_REQUEST_SYNC

	# Remove all fio's leftover state files
	if [ -n "$tmpdir" ] ; then
		log_must rm -fd "$tmpdir"/*.state "$tmpdir"
	fi
}

log_onexit cleanup

log_assert "Stress test zvols"

set_blk_mq 0
create_zvols
# Do some fio write/verifies in parallel
do_zvol_stress
destroy_zvols

# Enable blk-mq (block multi-queue), and re-run the same test
set_blk_mq 1
create_zvols
do_zvol_stress
destroy_zvols

# Disable zvol sync mode, and re-run test
set_zvol_sync 0
create_zvols
do_zvol_stress
destroy_zvols

# Same for enabled zvol sync mode
set_zvol_sync 1
create_zvols
do_zvol_stress

# Inject some errors, and verify we see some IO errors in zpool status
sync_pool $TESTPOOL
for DISK in $DISKS ; do
	log_must zinject -d $DISK -f 10 -e io -T write $TESTPOOL
done
log_must dd if=/dev/urandom of=$ZVOL_DEVDIR/$TESTPOOL/testvol1 bs=16M count=1
sync_pool $TESTPOOL
log_must zinject -c all

# We should see write errors
typeset -i write_errors=$(zpool status -p | awk '
	!NF { isvdev = 0 }
	isvdev { errors += $4 }
	/CKSUM$/ { isvdev = 1 }
	END { print errors }
')

if [ $write_errors -eq 0 ] ; then
	log_fail "Expected to see some write errors"
else
	log_note "Correctly saw $write_errors write errors"
fi
log_pass "Done with zvol_stress"

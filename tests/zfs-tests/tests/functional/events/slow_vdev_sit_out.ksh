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

# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.

# DESCRIPTION:
#	Verify that vdevs 'sit out' when they are slow
#
# STRATEGY:
#	1. Create various raidz/draid pools
#	2. Inject delays into one of the disks
#	3. Verify disk is set to 'sit out' for awhile.
#	4. Wait for READ_SIT_OUT_SECS and verify sit out state is lifted.
#

. $STF_SUITE/include/libtest.shlib

function cleanup
{
	restore_tunable READ_SIT_OUT_SECS
	restore_tunable SIT_OUT_CHECK_INTERVAL
	log_must zinject -c all
	log_must zpool events -c
	destroy_pool $TESTPOOL2
	log_must rm -f $TEST_BASE_DIR/vdev.$$.*
}

log_assert "Verify sit_out works"

log_onexit cleanup

# shorten sit out period for testing
save_tunable READ_SIT_OUT_SECS
set_tunable32 READ_SIT_OUT_SECS 5

save_tunable SIT_OUT_CHECK_INTERVAL
set_tunable64 SIT_OUT_CHECK_INTERVAL 20

log_must truncate -s200M $TEST_BASE_DIR/vdev.$$.{0..9}

for raidtype in raidz raidz2 raidz3 draid1 draid2 draid3 ; do
	log_must zpool create $TESTPOOL2 $raidtype $TEST_BASE_DIR/vdev.$$.{0..9}
	log_must zpool set autosit=on $TESTPOOL2 "${raidtype}-0"
	log_must dd if=/dev/urandom of=/$TESTPOOL2/bigfile bs=1M count=600
	log_must zpool export $TESTPOOL2
	log_must zpool import -d $TEST_BASE_DIR $TESTPOOL2

	BAD_VDEV=$TEST_BASE_DIR/vdev.$$.9

	# Initial state should not be sitting out
	log_must eval [[ "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV)" == "off" ]]

	# Delay our reads 200ms to trigger sit out
	log_must zinject -d $BAD_VDEV -D200:1 -T read $TESTPOOL2

	# Do some reads and wait for us to sit out
	for i in {0..99} ; do
		dd if=/$TESTPOOL2/bigfile skip=$i bs=2M count=1 of=/dev/null &
		dd if=/$TESTPOOL2/bigfile skip=$((i + 100)) bs=2M count=1 of=/dev/null &
		dd if=/$TESTPOOL2/bigfile skip=$((i + 200)) bs=2M count=1 of=/dev/null

		sit_out=$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV)
		if [[ "$sit_out" == "on" ]] ; then
			break
		fi
	done

	log_must test "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV)" == "on"

	# Clear fault injection
	log_must zinject -c all

	# Wait for us to exit our sit out period
	log_must wait_sit_out $TESTPOOL2 $BAD_VDEV 10

	# Verify sit_out was cleared during wait_sit_out
	log_must test "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV)" == "off"

	destroy_pool $TESTPOOL2
done

log_pass "sit_out works correctly"

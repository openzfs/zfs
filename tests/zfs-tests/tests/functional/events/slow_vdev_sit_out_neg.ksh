#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2025 by Klara, Inc.

# DESCRIPTION:
#	Verify that we don't sit out too many vdevs
#
# STRATEGY:
#	1. Create draid2 pool
#	2. Inject delays into three of the disks
#	3. Do reads to trigger sit-outs
#	4. Verify exactly 2 disks sit out
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

save_tunable SIT_OUT_CHECK_INTERVAL
set_tunable64 SIT_OUT_CHECK_INTERVAL 20

log_must truncate -s 150M $TEST_BASE_DIR/vdev.$$.{0..9}

log_must zpool create $TESTPOOL2 draid2 $TEST_BASE_DIR/vdev.$$.{0..9}
log_must zpool set autosit=on $TESTPOOL2 draid2-0
log_must dd if=/dev/urandom of=/$TESTPOOL2/bigfile bs=1M count=400
log_must zpool export $TESTPOOL2
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL2

BAD_VDEV1=$TEST_BASE_DIR/vdev.$$.7
BAD_VDEV2=$TEST_BASE_DIR/vdev.$$.8
BAD_VDEV3=$TEST_BASE_DIR/vdev.$$.9

# Initial state should not be sitting out
log_must eval [[ "$(get_vdev_prop autosit $TESTPOOL2 draid2-0)" == "on" ]]
log_must eval [[ "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV1)" == "off" ]]
log_must eval [[ "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV2)" == "off" ]]
log_must eval [[ "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV3)" == "off" ]]

# Delay our reads 200ms to trigger sit out
log_must zinject -d $BAD_VDEV1 -D200:1 -T read $TESTPOOL2

# Do some reads and wait for us to sit out
for i in {0..99} ; do
	dd if=/$TESTPOOL2/bigfile skip=$i bs=2M count=1 of=/dev/null &
	dd if=/$TESTPOOL2/bigfile skip=$((i + 100)) bs=2M count=1 of=/dev/null

	sit_out=$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV1)
	if [[ "$sit_out" == "on" ]] ; then
		break
	fi
done
log_must test "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV1)" == "on"

log_must zinject -d $BAD_VDEV2 -D200:1 -T read $TESTPOOL2
# Do some reads and wait for us to sit out
for i in {0..99} ; do
	dd if=/$TESTPOOL2/bigfile skip=$i bs=2M count=1 of=/dev/null &
	dd if=/$TESTPOOL2/bigfile skip=$((i + 100)) bs=2M count=1 of=/dev/null

	sit_out=$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV2)
	if [[ "$sit_out" == "on" ]] ; then
		break
	fi
done
log_must test "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV2)" == "on"

log_must zinject -d $BAD_VDEV3 -D200:1 -T read $TESTPOOL2
# Do some reads and wait for us to sit out
for i in {0..99} ; do
	dd if=/$TESTPOOL2/bigfile skip=$i bs=2M count=1 of=/dev/null &
	dd if=/$TESTPOOL2/bigfile skip=$((i + 100)) bs=2M count=1 of=/dev/null

	sit_out=$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV3)
	if [[ "$sit_out" == "on" ]] ; then
		break
	fi
done
log_must test "$(get_vdev_prop sit_out $TESTPOOL2 $BAD_VDEV3)" == "off"


log_pass "sit_out works correctly"

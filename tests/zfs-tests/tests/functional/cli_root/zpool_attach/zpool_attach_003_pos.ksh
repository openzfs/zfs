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
# Copyright (c) 2025 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool attach' expands size correctly with anyraid vdevs.
#
# STRATEGY:
# 1. Create an anymirror1 vdev with small disks
# 2. Attach larger disk
# 3. Verify that not all the new space can be used
# 4. Attach another larger disk
# 5. Verify that all space is now usable
# 6. Repeat steps 1-5 with anymirror2
#

verify_runnable "global"

cleanup() {
	log_must zpool destroy $TESTPOOL2
	rm /$TESTPOOL/vdev_file.*
	restore_tunable ANYRAID_MIN_TILE_SIZE
}

log_onexit cleanup

log_must truncate -s 512M $TEST_BASE_DIR/vdev_file.{0,1,2,3}
log_must truncate -s 2G $TEST_BASE_DIR/vdev_file.{4,5,6}
save_tunable ANYRAID_MIN_TILE_SIZE
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "'zpool attach' expands size correctly with anyraid vdevs"

log_must create_pool $TESTPOOL2 anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2}

cap=$(zpool get -Hp -o value size $TESTPOOL2)
log_must zpool attach $TESTPOOL2 anymirror1-0 $TEST_BASE_DIR/vdev_file.4
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))

[[ "$new_cap" -eq $((3 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool attach $TESTPOOL2 anymirror1-0 $TEST_BASE_DIR/vdev_file.5
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))
[[ "$new_cap" -eq $(((2048 - 256 - 64) * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool destroy $TESTPOOL2
log_must create_pool $TESTPOOL2 anymirror2 $TEST_BASE_DIR/vdev_file.{0,1,2,3}

cap=$(zpool get -Hp -o value size $TESTPOOL2)
log_must zpool attach $TESTPOOL2 anymirror2-0 $TEST_BASE_DIR/vdev_file.4
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))

[[ "$new_cap" -eq $((64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool attach $TESTPOOL2 anymirror2-0 $TEST_BASE_DIR/vdev_file.5
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))
[[ "$new_cap" -eq $((256 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool attach $TESTPOOL2 anymirror2-0 $TEST_BASE_DIR/vdev_file.6
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))
[[ "$new_cap" -eq $(((2048 - 256 - 64) * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_pass "'zpool attach' expands size correctly with anyraid vdevs"

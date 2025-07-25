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

#
# Copyright (c) 2025 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool attach' expands size correctly with anyraid vdevs.
#
# STRATEGY:
# 1. Create an anyraid1 vdev with small disks
# 2. Attach larger disk
# 3. Verify that not all the new space can be used
# 4. Attach another larger disk
# 5. Verify that all space is now usable
# 6. Repeat steps 1-5 with anyraid2
#

verify_runnable "global"

cleanup() {
	log_must zpool destroy $TESTPOOL2
	rm /$TESTPOOL/vdev_file.*
	restore_tunable ANYRAID_MIN_TILE_SIZE
}

log_onexit cleanup

log_must truncate --size=512M /$TESTPOOL/vdev_file.{0,1,2,3}
log_must truncate --size=2G /$TESTPOOL/vdev_file.{4,5,6}
save_tunable ANYRAID_MIN_TILE_SIZE
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "'zpool attach' expands size correctly with anyraid vdevs"

log_must create_pool $TESTPOOL2 anyraid1 /$TESTPOOL/vdev_file.{0,1,2}

cap=$(zpool get -Hp -o value size $TESTPOOL2)
log_must zpool attach $TESTPOOL2 anyraid1-0 /$TESTPOOL/vdev_file.4
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))

[[ "$new_cap" -eq $((3 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool attach $TESTPOOL2 anyraid1-0 /$TESTPOOL/vdev_file.5
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))
[[ "$new_cap" -eq $(((2048 - 256 - 64) * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool destroy $TESTPOOL2
log_must create_pool $TESTPOOL2 anyraid2 /$TESTPOOL/vdev_file.{0,1,2,3}

cap=$(zpool get -Hp -o value size $TESTPOOL2)
log_must zpool attach $TESTPOOL2 anyraid2-0 /$TESTPOOL/vdev_file.4
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))

[[ "$new_cap" -eq $((64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool attach $TESTPOOL2 anyraid2-0 /$TESTPOOL/vdev_file.5
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))
[[ "$new_cap" -eq $((256 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_must zpool attach $TESTPOOL2 anyraid2-0 /$TESTPOOL/vdev_file.6
new_cap=$(zpool get -Hp -o value size $TESTPOOL2)
new_cap=$((new_cap - cap))
[[ "$new_cap" -eq $(((2048 - 256 - 64) * 1024 * 1024)) ]] || \
	log_fail "Incorrect space added on attach: $new_cap"

log_pass "'zpool attach' expands size correctly with anyraid vdevs"

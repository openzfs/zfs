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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that contraction fails gracefully when there is not enough
# free tile capacity on remaining disks to absorb the tiles from the
# disk being removed (ENOSPC or EXFULL).
#
# STRATEGY:
# 1. Create anymirror1 pool with 3 disks (768M each)
# 2. Fill the pool with data until nearly full
# 3. Attempt contraction to remove one disk
# 4. Verify command fails (ENOSPC or EXFULL)
# 5. Verify pool is still ONLINE and data intact
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Contraction fails when insufficient free tiles on remaining disks"

log_note "DEBUG: creating pool with 3 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Fill the pool with data until nearly full. With 3 disks at 768M
# and 64MiB tiles, anymirror1 has usable capacity spread across all
# disks. We write until we get close to full.
#
log_note "DEBUG: filling pool with data"
typeset -i fill_idx=0
while (( fill_idx < 20 )); do
	file_write -o create -b 1048576 -c 20 -d 'R' \
		-f /$TESTPOOL/fill.$fill_idx 2>/dev/null
	if [[ $? -ne 0 ]]; then
		log_note "DEBUG: pool full at fill.$fill_idx"
		break
	fi
	(( fill_idx = fill_idx + 1 ))
done
log_note "DEBUG: wrote $fill_idx fill files"

log_note "DEBUG: pool usage after fill"
zpool list -v $TESTPOOL

#
# Attempt contraction. With the pool nearly full, there should not
# be enough free tiles on the remaining 2 disks to absorb all tiles
# from the disk being removed.
#
log_note "DEBUG: attempting contraction on nearly-full pool"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.2
log_note "DEBUG: contraction correctly rejected (ENOSPC/EXFULL)"

#
# Verify pool is still ONLINE.
#
log_note "DEBUG: verifying pool still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Verify scrub finds no errors.
#
log_note "DEBUG: running scrub"
log_must zpool scrub -w $TESTPOOL

log_note "DEBUG: checking for checksum errors"
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Contraction fails when insufficient free tiles on remaining disks"

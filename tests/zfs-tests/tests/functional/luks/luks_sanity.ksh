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
# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

# DESCRIPTION:
#	Verify ZFS works on a LUKS-backed pool
#
# STRATEGY:
#	1. Create a LUKS device
#	2. Make a pool with it
#	3. Write files to the pool
#	4. Verify no errors

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

VDEV=$(mktemp --suffix=luks_sanity)
TESTPOOL=testpool

function cleanup
{
	log_must zpool destroy $TESTPOOL

	log_must cryptsetup luksClose /dev/mapper/luksdev
	log_must rm -f $VDEV
}

log_assert "Verify ZFS on LUKS works"
log_onexit cleanup

PASS="fdsjfosdijfsdkjsldfjdlk"

# Make a small LUKS device since LUKS formatting takes time and we want to
# make this test run as quickly as possible.
truncate -s 100M $VDEV

log_must cryptsetup luksFormat --type luks2 $VDEV <<< $PASS
log_must cryptsetup luksOpen $VDEV luksdev <<< $PASS

log_must zpool create $TESTPOOL /dev/mapper/luksdev

CPUS=$(get_num_cpus)

# Use these specific size and offset ranges as they often cause errors with
# https://github.com/openzfs/zfs/issues/16631
# and we want to try to test for that.
for SIZE in {70..100} ; do
	for OFF in {70..100} ; do
		for i in {1..$CPUS} ; do
			dd if=/dev/urandom of=/$TESTPOOL/file$i-bs$SIZE-off$OFF \
			    seek=$OFF bs=$SIZE count=1 &>/dev/null &
		done
		wait
	done
	sync_pool $TESTPOOL
	rm -f /$TESTPOOL/file*
done

# Verify no read/write/checksum errors.  Don't use JSON here so that we could
# could potentially backport this test case to the 2.2.x branch.
if zpool status -e | grep -q "luksdev" ; then
	log_note "$(zpool status -v)"
	log_fail "Saw errors writing to LUKS device"
fi

log_pass "Verified ZFS on LUKS works"

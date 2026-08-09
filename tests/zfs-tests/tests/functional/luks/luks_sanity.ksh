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

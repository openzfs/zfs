#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Deeply nested clones can be created and destroyed successfully
#
# STRATEGY:
# 1. Create a deeply nested chain of clones
# 2. Verify we can promote and destroy datasets in the chain without issues
# NOTE:
# Ported from scripts used to reproduce issue #3959 and #7279
#

verify_runnable "both"

function cleanup
{
	destroy_dataset "$clonesfs" "-rRf"
}
log_onexit cleanup

log_assert "Deeply nested clones should be created and destroyed without issues"

snapname='snap'
snaprename='temporary-snap'
clonesfs="$TESTPOOL/$TESTFS1"

# NOTE: set mountpoint=none to avoid mount/umount calls and speed up the process
log_must zfs create -o mountpoint=none $clonesfs
log_must zfs create $clonesfs/0
dsname="$clonesfs/0@$snapname"
log_must zfs snapshot $dsname

# 1. Create a deeply nested chain of clones
for c in {1..250}; do
	log_must zfs clone $dsname $clonesfs/$c
	dsname="$clonesfs/$c@$snapname"
	log_must zfs snapshot $dsname
done

# 2. Verify we can promote and destroy datasets in the chain without issues
for c in {0..249}; do
	log_must zfs rename $clonesfs/$c@$snapname $clonesfs/$c@$snaprename
	log_must zfs promote $clonesfs/$((c+1))
	log_must zfs destroy -r $clonesfs/$c
	log_must zfs destroy $clonesfs/$((c+1))@$snaprename
done

log_pass "Deeply nested clones can be created and destroyed successfully"

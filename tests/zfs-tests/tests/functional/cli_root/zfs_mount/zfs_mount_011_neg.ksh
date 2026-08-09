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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs mount should fail with bad parameters
#
# STRATEGY:
# 1. Make an array of bad parameters
# 2. Use zfs mount to mount the filesystem
# 3. Verify that zfs mount returns error
#

verify_runnable "both"

function cleanup
{
	snapexists $TESTPOOL/$TESTFS@$TESTSNAP && \
		destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP

	if is_global_zone && datasetexists $TESTPOOL/$TESTVOL; then
		destroy_dataset $TESTPOOL/$TESTVOL
	fi
}

log_assert "zfs mount fails with bad parameters"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
set -A badargs "A" "-A" "-" "-x" "-?" "=" "-o *" "-a"

for arg in "${badargs[@]}"; do
	log_mustnot eval "zfs mount $arg $fs >/dev/null 2>&1"
done

#verify that zfs mount fails with invalid dataset
for opt in "-o abc" "-O"; do
	log_mustnot eval "zfs mount $opt /$fs >/dev/null 2>&1"
done

#verify that zfs mount fails with volume and snapshot
log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_mustnot eval "zfs mount $TESTPOOL/$TESTFS@$TESTSNAP >/dev/null 2>&1"

if is_global_zone; then
	log_must zfs create -V 10m $TESTPOOL/$TESTVOL
	log_mustnot eval "zfs mount $TESTPOOL/$TESTVOL >/dev/null 2>&1"
fi

log_pass "zfs mount fails with bad parameters as expected."

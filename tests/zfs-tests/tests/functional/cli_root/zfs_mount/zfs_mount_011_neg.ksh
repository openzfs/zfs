#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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

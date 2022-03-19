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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

# DESCRIPTION:
#	zfs send with multiple -X/--exclude options will
#	exclude all of them.
#
# STRATEGY:
#	1. Setup test model
#	2. Create several datasets on pool.
#	3. Send -R -X pool/dataset
#	4. Verify receive does not have the excluded dataset(s).

verify_runnable "both"

function cleanup
{
    cleanup_pool $POOL2
    cleanup_pool $POOL
    log_must setup_test_model $POOL
}

log_assert "zfs send with multiple -X options will skip excluded dataset"
log_onexit cleanup

cleanup

#
# Create some datasets
log_must zfs create -p $POOL/ds1/second/third
log_must zfs create -p $POOL/ds2/second
log_must zfs create -p $POOL/ds3/first/second/third

log_must zfs snapshot -r $POOL@presend

log_must eval "zfs send -R $POOL@presend > $BACKDIR/presend"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/presend"

for ds in ds1 ds1/second ds1/second/third \
	      ds2 ds2/second \
	      ds3 ds3/first ds3/first/second ds3/first/second/third
do
    log_must datasetexists $POOL2/$ds
done

log_must_busy zfs destroy -r $POOL2

log_must eval "zfs send -R -X $POOL/ds1/second --exclude $POOL/ds3/first/second $POOL@presend > $BACKDIR/presend"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/presend"

for ds in ds1 ds2 ds2/second ds3 ds3/first
do
    log_must datasetexists $POOL2/$ds
done

for ds in ds1/second ds1/second/third ds3/first/second ds3/first/second/third
do
    log_must datasetnonexists $POOL2/$ds
done

log_pass "zfs send with multiple -X options  excluded datasets"


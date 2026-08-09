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


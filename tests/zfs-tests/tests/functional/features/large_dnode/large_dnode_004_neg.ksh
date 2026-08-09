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
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

TEST_FS=$TESTPOOL/large_dnode
TEST_SNAP=$TESTPOOL/large_dnode@ldnsnap
TEST_STREAM=$TESTDIR/ldnsnap

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS -r

	if datasetexists $LGCYPOOL ; then
		log_must zpool destroy -f $LGCYPOOL
	fi

	rm -f $TEST_STREAM
}

log_onexit cleanup
log_assert "zfs send stream with large dnodes not accepted by legacy pool"

log_must zfs create -o dnodesize=1k $TEST_FS
log_must touch /$TEST_FS/foo
log_must zfs umount $TEST_FS
log_must zfs snap $TEST_SNAP
log_must eval "zfs send $TEST_SNAP > $TEST_STREAM"

LGCYPOOL=ldnpool
LGCYFS=$LGCYPOOL/legacy
log_must mkfile 64M  $TESTDIR/$LGCYPOOL
log_must zpool create -d $LGCYPOOL $TESTDIR/$LGCYPOOL
log_mustnot eval "zfs recv $LGCYFS < $TEST_STREAM"

log_pass

#!/bin/ksh -p
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

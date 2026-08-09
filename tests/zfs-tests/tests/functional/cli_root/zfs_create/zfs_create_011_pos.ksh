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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs create -p'  should work as expected
#
# STRATEGY:
# 1. To create $newdataset with -p option, first make sure the upper level
#    of $newdataset does not exist
# 2. Make sure without -p option, 'zfs create' will fail
# 3. Create $newdataset with -p option, verify it is created
# 4. Run 'zfs create -p $newdataset' again, the exit code should be zero
#    even $newdataset exists
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -rf
}

log_onexit cleanup

typeset newdataset1="$TESTPOOL/$TESTFS1/$TESTFS/$TESTFS1"
typeset newdataset2="$TESTPOOL/$TESTFS1/$TESTFS/$TESTVOL1"

log_assert "'zfs create -p' works as expected."

log_must verify_opt_p_ops "create" "fs" $newdataset1

# verify volume creation
if is_global_zone; then
	log_must verify_opt_p_ops "create" "vol" $newdataset2
fi

log_pass "'zfs create -p' works as expected."

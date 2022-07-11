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

#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
. $STF_SUITE/tests/functional/cli_root/zfs_rename/zfs_rename.kshlib

#
# DESCRIPTION
#       'zfs rename -p' should work as expected
#
# STRATEGY:
#	1. Make sure the upper level of $newdataset does not exist
#       2. Make sure without -p option, 'zfs rename' will fail
#       3. With -p option, rename works
#

verify_runnable "both"

function additional_cleanup
{
	datasetexists $TESTPOOL/notexist && \
		destroy_dataset $TESTPOOL/notexist -Rf

	datasetexists $TESTPOOL/$TESTFS && \
		destroy_dataset $TESTPOOL/$TESTFS -Rf

	log_must zfs create $TESTPOOL/$TESTFS

	if is_global_zone ; then
		datasetexists $TESTPOOL/$TESTVOL && \
			destroy_dataset $TESTPOOL/$TESTVOL -Rf

		log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL
	fi
}

log_onexit additional_cleanup

log_assert "'zfs rename -p' should work as expected"

log_must verify_opt_p_ops "rename" "fs" "$TESTPOOL/$TESTFS" \
	"$TESTPOOL/notexist/new/$TESTFS1"

if is_global_zone; then
	log_must verify_opt_p_ops "rename" "vol" "$TESTPOOL/$TESTVOL" \
		"$TESTPOOL/notexist/new/$TESTVOL1"
fi

log_pass "'zfs rename -p' should work as expected"

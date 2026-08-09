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

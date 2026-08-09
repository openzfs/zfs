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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

#
# DESCRIPTION:
#	'zpool destroy' failed if this filesystem is namespace-parent
#	of origin.
#
# STRATEGY:
#	1. Create pool, fs and snapshot.
#	2. Create a namespace-parent of origin clone.
#	3. Promote this clone
#	4. Verify the original fs can not be destroyed.
#

verify_runnable "both"

function cleanup
{
	if datasetexists $clonesnap; then
		log_must zfs promote $fs
	fi
	datasetexists $clone && destroy_dataset $clone
	datasetexists $fssnap && destroy_dataset $fssnap
}

log_assert "Destroy dataset which is namespace-parent of origin should failed."
log_onexit cleanup

# Define variable $fssnap & and namespace-parent of origin clone.
fs=$TESTPOOL/$TESTFS
fssnap=$fs@snap
clone=$fs/clone
clonesnap=$fs/clone@snap

# Define key word for expected failure.
KEY_WORDS="filesystem has children"

log_must zfs snapshot $fssnap
log_must zfs clone $fssnap $clone
log_must zfs promote $clone
log_mustnot_expect "$KEY_WORDS" zfs destroy $fs
log_mustnot_expect "$KEY_WORDS" zfs destroy $clone

log_pass "Destroy dataset which is namespace-parent of origin passed."

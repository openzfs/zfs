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

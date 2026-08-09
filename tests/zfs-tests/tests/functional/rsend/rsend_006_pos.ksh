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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	Rename snapshot name will not change the dependent order.
#
# STRATEGY:
#	1. Set up a set of datasets.
#	2. Rename part of snapshots.
#	3. Send -R all the POOL
#	4. Verify snapshot name will not change the dependent order.
#

verify_runnable "both"

#		Source			Target
#
set -A	snaps	"$POOL@init"		"$POOL@snap0"	\
		"$POOL@snapA"		"$POOL@snap1"	\
		"$POOL@snapC"		"$POOL@snap2"	\
		"$POOL@final"		"$POOL@init"

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must setup_test_model $POOL
}

log_assert "Rename snapshot name will not change the dependent order."
log_onexit cleanup

typeset -i i=0
while ((i < ${#snaps[@]})); do
	log_must zfs rename -r ${snaps[$i]} ${snaps[((i+1))]}

	((i += 2))
done

#
# Duplicate POOL2 for testing
#
log_must eval "zfs send -R $POOL@init > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

log_pass "Rename snapshot name will not change the dependent order."

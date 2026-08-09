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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs rename -r can rename snapshot when child datasets
#	don't have a snapshot of the given name.
#
# STRATEGY:
#	1. Create snapshot.
#	2. Rename snapshot recursively.
#	3. Verify rename -r snapshot correctly.
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTCTR@snap-new && \
		destroy_dataset $TESTPOOL/$TESTCTR@snap-new -f

	datasetexists $TESTPOOL/$TESTCTR@snap && \
		destroy_dataset $TESTPOOL/$TESTCTR@snap -f

	datasetexists $TESTPOOL@snap-new && \
		destroy_dataset $TESTPOOL@snap-new -f

	datasetexists $TESTPOOL@snap && \
		destroy_dataset $TESTPOOL@snap -f
}

log_assert "zfs rename -r can rename snapshot when child datasets" \
	"don't have a snapshot of the given name."

log_onexit cleanup

log_must zfs snapshot $TESTPOOL/$TESTCTR@snap
log_must zfs rename -r $TESTPOOL/$TESTCTR@snap $TESTPOOL/$TESTCTR@snap-new
log_must datasetexists $TESTPOOL/$TESTCTR@snap-new

log_must zfs snapshot $TESTPOOL@snap
log_must zfs rename -r $TESTPOOL@snap $TESTPOOL@snap-new
log_must datasetexists $TESTPOOL/$TESTCTR@snap-new
log_must datasetexists $TESTPOOL@snap-new

log_must zfs destroy -f $TESTPOOL/$TESTCTR@snap-new
log_must zfs destroy -f $TESTPOOL@snap-new

log_pass "Verify zfs rename -r passed when child datasets" \
	"don't have a snapshot of the given name."


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


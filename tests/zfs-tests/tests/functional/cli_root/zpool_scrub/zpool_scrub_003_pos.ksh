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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
# Copyright (c) 2017 by Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	scrub command fails when there is an existing scrub in progress
#
# STRATEGY:
#	1. Setup a pool and fill it with data
#	2. Kick off a scrub
#	2. Kick off a second scrub and verify it fails
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
}

log_onexit cleanup

log_assert "Scrub command fails when there is already a scrub in progress"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL true
log_mustnot zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL true
log_must zpool scrub -s $TESTPOOL
log_must is_pool_scrub_stopped $TESTPOOL true

log_pass "Issuing a scrub command failed when scrub was already in progress"

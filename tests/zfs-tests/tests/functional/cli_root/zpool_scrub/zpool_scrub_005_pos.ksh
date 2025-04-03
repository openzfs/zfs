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
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	When scrubbing, detach device should not break system.
#
# STRATEGY:
#	1. Setup filesys with data.
#	2. Detaching and attaching the device when scrubbing.
#	3. Try it twice, verify both of them work fine.
#

verify_runnable "global"

log_assert "When scrubbing, detach device should not break system."

log_must zpool scrub $TESTPOOL
log_must zpool detach $TESTPOOL $DISK2
log_must zpool attach -w $TESTPOOL $DISK1 $DISK2

log_must zpool scrub $TESTPOOL
log_must zpool detach $TESTPOOL $DISK1
log_must zpool attach -w $TESTPOOL $DISK2 $DISK1

log_pass "When scrubbing, detach device should not break system."

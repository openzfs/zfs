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

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

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# Exported pools should no longer be visible from 'zpool list'.
# Therefore, we export an existing pool and verify it cannot
# be accessed.
#
# STRATEGY:
# 1. Unmount the test directory.
# 2. Export the pool.
# 3. Verify the pool is no longer present in the list output.
#

verify_runnable "global"

log_onexit zpool_export_cleanup

log_assert "Verify a pool can be exported."

log_must zfs umount $TESTDIR
log_must zpool export $TESTPOOL

poolexists $TESTPOOL && \
        log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully exported a ZPOOL."

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

. $STF_SUITE/tests/functional/dirsync/dirsync.kshlib

#
# DESCRIPTION:
# Namespace operations commit the ZIL when dirsync=on, and do not when
# dirsync=off or sync=disabled.
#
# STRATEGY:
# 1. Verify that dirsync is off by default.
# 2. Run each namespace operation and verify that zil_commit_count does
#    not change.
# 3. Set dirsync=on and verify that each operation increases it.
# 4. Set sync=disabled and verify that it does not change.
#

verify_runnable "global"

function cleanup
{
	zfs inherit dirsync $TESTPOOL/$TESTFS
	zfs inherit sync $TESTPOOL/$TESTFS
	dirsync_cleanup $TESTDIR
}

log_assert "Namespace operations commit the ZIL with dirsync=on"
log_onexit cleanup

log_must test "$(get_prop dirsync $TESTPOOL/$TESTFS)" = "off"
dirsync_ops $TESTPOOL/$TESTFS $TESTDIR no

log_must zfs set dirsync=on $TESTPOOL/$TESTFS
dirsync_ops $TESTPOOL/$TESTFS $TESTDIR yes

log_must zfs set sync=disabled $TESTPOOL/$TESTFS
dirsync_ops $TESTPOOL/$TESTFS $TESTDIR no

log_pass "Namespace operations commit the ZIL with dirsync=on"

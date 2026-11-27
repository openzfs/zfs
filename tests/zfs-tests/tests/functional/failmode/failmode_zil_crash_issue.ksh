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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# When the pool suspends while zil_commit() fills a log block, the ZIL
# crashes before it issues the log block.  Once the pool resumes, the ZIL
# frees that log block and restarts.  It used to free the log block in the
# "closed" state, which fails an assertion in zil_free_lwb().
#
# STRATEGY:
# 1. Run zil_crash_issue, which does this in userspace (libzpool) on a
#    pool of one file vdev, and checks that sync writes go through the ZIL
#    again.
#

verify_runnable "global"

typeset dir=$TEST_BASE_DIR/failmode_zil_crash_issue

function cleanup
{
	rm -rf $dir
}

log_onexit cleanup

log_assert "the ZIL restarts after it crashed while filling a log block"

log_must mkdir -p $dir
log_must zil_crash_issue $dir

log_pass "the ZIL restarts after it crashed while filling a log block"

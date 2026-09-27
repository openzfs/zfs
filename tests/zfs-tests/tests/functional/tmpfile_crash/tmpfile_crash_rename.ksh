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

. $STF_SUITE/tests/functional/tmpfile_crash/tmpfile_crash.kshlib

#
# DESCRIPTION:
#	O_TMPFILE publication is recoverable from the intent log alone:
#	the file is published under a temporary name, then renamed and the
#	directory synced; exactly one complete name may exist.
#
# STRATEGY:
#	1. Create a pool and run the "rename" scenario of
#	   tmpfile_crash with the pool frozen at its START barrier.
#	2. At ACK, copy the vdevs while the pool is imported, import the copy
#	   (replaying its intent log) and verify it.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash image: rename"
log_onexit tmpfile_crash_cleanup

tmpfile_crash_image rename

log_pass "O_TMPFILE publication crash image: rename"

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
#	O_TMPFILE publication recovers from the intent log alone after a
#	crash: data fsynced, then linkat(2) and fsync(2) of the directory.
#
# STRATEGY:
#	1. Create a pool on a file vdev, freeze it after scenario setup.
#	2. Run the "dirsync" scenario of tmpfile_crash until it acknowledges.
#	3. Copy the vdev while the pool is imported (the crash image).
#	4. Import the crash image, replaying its intent log.
#	5. Verify the recovered state the scenario was promised.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash image: dirsync"
log_onexit tmpfile_crash_cleanup

tmpfile_crash_image dirsync

log_pass "O_TMPFILE publication crash image: dirsync"

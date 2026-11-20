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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/failmode/failmode.kshlib

#
# DESCRIPTION:
#	A raw receive of a new encrypted dataset that is interrupted by a
#	pool suspension (failmode=wait) completes once the pool is cleared,
#	and the received data is intact (#18394), also when its key is
#	loaded during the receive and `zpool status -v` holds the dataset
#	with its key mapped, in a loop, meanwhile.
#
# STRATEGY:
#	1. Create an encrypted dataset with many small files and a large one
#	   on a source pool, snapshot it and save a raw send stream
#	2. Create a single-disk destination pool with failmode=wait
#	3. Receive most of the stream (-s -u) and sync the pool; load the
#	   key and start a `zpool status -v` loop
#	4. Inject write errors and probe failures, and feed some more of the
#	   stream: the next txg sync fails and the pool suspends
#	5. Clear the injection and the pool, and feed the rest of the stream
#	6. The receive must succeed, and the pool must stay healthy
#	7. Mount the dataset: the files must match the source, also after an
#	   export and import
#	8. A scrub must find no errors
#
# See failmode_raw_recv_resume() in failmode.kshlib.
#

verify_runnable "global"

typeset -i FAILMODE_RECV_PID=0
typeset -i FAILMODE_STATUS_PID=0

log_onexit failmode_raw_recv_resume_cleanup

log_assert "A raw receive with its key loaded survives a pool suspension"

failmode_raw_recv_resume 1

log_pass "A raw receive with its key loaded survives a pool suspension"

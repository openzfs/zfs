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

. $STF_SUITE/tests/functional/tmpfile_dmlog/tmpfile_dmlog.kshlib

verify_runnable "global"

tmpfile_dmlog_cleanup
# Backstop: the failsafe callback restores a killed test's saved tunables
# before the next test; the replay test hook is default-off in any case.
if tunable_exists ZIL_REPLAY_SYNC_PER_RECORD; then
	log_must set_tunable32 ZIL_REPLAY_SYNC_PER_RECORD 0
fi
log_pass

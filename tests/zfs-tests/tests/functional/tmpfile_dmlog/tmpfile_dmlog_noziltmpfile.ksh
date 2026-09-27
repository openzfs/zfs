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

#
# DESCRIPTION:
#	Without the ziltmpfile feature, O_TMPFILE publication must not log
#	TX_TMPFILE (older software could not replay it) and must still be
#	durable, by waiting for a TXG.
#
# STRATEGY:
#	1. Create a pool on a dm-log-writes device.
#	2. Run the "dirsync" scenario of tmpfile_crash, marking the log at its
#	   START barrier and after it acknowledges.
#	3. Replay the log to each recorded FLUSH/FUA between the marks and
#	   to the ACK mark; import a copy of each state and verify it.
#

verify_runnable "global"

if ! zpool upgrade -v | grep -qw ziltmpfile; then
	log_unsupported "the ziltmpfile feature is not supported"
fi

log_assert "O_TMPFILE publication crash states: noziltmpfile"
log_onexit tmpfile_dmlog_cleanup

DMLOG_POOL_OPTS="-o feature@ziltmpfile=disabled"
DMLOG_FORBID_RECORD=TX_TMPFILE
tmpfile_dmlog dirsync

log_pass "O_TMPFILE publication crash states: noziltmpfile"

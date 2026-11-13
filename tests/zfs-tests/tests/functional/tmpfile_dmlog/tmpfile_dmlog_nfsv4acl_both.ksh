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
#	O_TMPFILE publication is recoverable at each recorded flush point
#	when the unnamed file is opened in one directory with inheritable
#	native (NFSv4) ACEs, made on FreeBSD, and published into another:
#	publication must wait for its TXG (no TX_TMPFILE at any state), and
#	the recovered file must keep a non-trivial ACL.
#
# STRATEGY:
#	1. Create a pool on a dm-log-writes device and receive the
#	   nfsv4acl stream as its file system.
#	2. Run the "nfsv4both" scenario of tmpfile_crash, marking the log at its
#	   START barrier and after it acknowledges.
#	3. Replay the log to each recorded FLUSH/FUA between the marks and
#	   to the ACK mark; import a copy of each state and verify it, and
#	   check the records the publication was logged with and,
#	   at ACK, the pflags of the recovered file and the directories.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash states: nfsv4acl_both"
log_onexit tmpfile_dmlog_cleanup

# Both ACLs are non-trivial, so the pflags check cannot tell the original
# from the one replay would build; the absence of TX_TMPFILE is the check.
DMLOG_FS_STREAM=$DMLOG_NFSV4_STREAM
DMLOG_FS_OPTS="-o aclinherit=passthrough"
DMLOG_FORBID_RECORD=TX_TMPFILE
DMLOG_ACK_PFLAGS="/inh2/f:4:0 /inh:2:2 /inh2:2:2"
tmpfile_dmlog nfsv4both

log_pass "O_TMPFILE publication crash states: nfsv4acl_both"

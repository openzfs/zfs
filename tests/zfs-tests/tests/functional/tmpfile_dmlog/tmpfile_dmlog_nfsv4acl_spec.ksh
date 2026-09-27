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
#	when the unnamed file is opened in a directory, made on FreeBSD,
#	whose inherit-only owner@, group@ and everyone@ entries give it the
#	masks of its mode, marked inherited, and it is published into a
#	plain directory: the ACL passes ZFS_ACL_TRIVIAL but is not the one
#	its mode gives a new file, so publication must wait for its TXG (no
#	TX_TMPFILE at any state).
#
# STRATEGY:
#	1. Create a pool on a dm-log-writes device and receive the
#	   nfsv4acl stream as its file system.
#	2. Run the "nfsv4spec" scenario of tmpfile_crash, marking the log at its
#	   START barrier and after it acknowledges.
#	3. Replay the log to each recorded FLUSH/FUA between the marks and
#	   to the ACK mark; import a copy of each state and verify it, and
#	   check the records the publication was logged with and,
#	   at ACK, the pflags of the recovered file and the directories.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash states: nfsv4acl_spec"
log_onexit tmpfile_dmlog_cleanup

# The recovered file counts as trivial either way: the pflags check cannot
# tell its inherited entries from the ones replay would build from its mode;
# the absence of TX_TMPFILE is the check.
DMLOG_FS_STREAM=$DMLOG_NFSV4_STREAM
DMLOG_FS_OPTS="-o aclinherit=passthrough"
DMLOG_FORBID_RECORD=TX_TMPFILE
DMLOG_ACK_PFLAGS="/plain/f:4:4 /inhspec:2:2 /plain:2:0"
tmpfile_dmlog nfsv4spec

log_pass "O_TMPFILE publication crash states: nfsv4acl_spec"

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
#	when the unnamed file is given a POSIX access ACL through its xattr,
#	which changes its mode without rebuilding its native ACL, and is
#	published without a chmod: the native ACL is not the one its mode
#	gives a new file, so publication must wait for its TXG (no
#	TX_TMPFILE at any state), and mode and ACL must survive.
#
# STRATEGY:
#	1. Create a pool on a dm-log-writes device, acltype=posix.
#	2. Run the "posixacl" scenario of tmpfile_crash, marking the log at its
#	   START barrier and after it acknowledges.
#	3. Replay the log to each recorded FLUSH/FUA between the marks and
#	   to the ACK mark; import a copy of each state and verify it, and
#	   check the records the publication was logged with.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash states: posixacl"
log_onexit tmpfile_dmlog_cleanup

DMLOG_FS_OPTS="-o acltype=posix"
DMLOG_FORBID_RECORD=TX_TMPFILE
tmpfile_dmlog posixacl

log_pass "O_TMPFILE publication crash states: posixacl"

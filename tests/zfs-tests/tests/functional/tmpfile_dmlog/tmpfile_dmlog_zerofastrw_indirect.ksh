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
#	O_TMPFILE publication is recoverable at each recorded flush point when
#	an interior block of the published file is zeroed
#	(FALLOC_FL_ZERO_RANGE) and rewritten with other bytes before the
#	publication is committed, and the file is then synced, with
#	zfs_immediate_write_sz=0 (indirect write logging).
#	Every state has no file (before ACK) or the file with its other blocks
#	as published; at ACK the block must hold the rewritten bytes.  The
#	publication's record for that block must not resolve to the rewrite
#	ahead of the zeroing (see zfs_tmpfile_settle()).
#
# STRATEGY:
#	1. Create a pool on a dm-log-writes device.
#	2. Run the "zerofastrw" scenario of tmpfile_crash: write and publish
#	   without a sync, zero a block, rewrite it, fsync the file.
#	   Mark the log at its START barrier and after it acknowledges.
#	3. Replay the log to each recorded FLUSH/FUA between the marks and
#	   to the ACK mark; import a copy of each state and verify it, with
#	   the publication and the zeroing (TX_TRUNCATE) logged.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash states: zerofastrw_indirect"
log_onexit tmpfile_dmlog_cleanup

DMLOG_EXPECT_RECORD="TX_TMPFILE TX_TRUNCATE"
tmpfile_dmlog zerofastrw sa 0

log_pass "O_TMPFILE publication crash states: zerofastrw_indirect"

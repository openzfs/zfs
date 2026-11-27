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
#	an unaligned range of the published file, ending in the next block, is
#	punched out and a smaller range inside it, across the same block
#	boundary, rewritten, and only an unrelated file is then synced: that
#	commits the publication's records, which are synchronous, but not the
#	punch's or the rewrite's, with zfs_immediate_write_sz=0 (indirect
#	write logging).  Every state has no file (before ACK) or the whole
#	file in one state it went through; at ACK, f must be present, as
#	published, cleared or rewritten.  Neither TX_WRITE2 nor the clearing
#	is logged.
#
# STRATEGY:
#	1. Create a pool on a dm-log-writes device.
#	2. Run the "punchmark" scenario of tmpfile_crash: write and publish
#	   without a sync, punch the range, rewrite part of it, fsync only a
#	   marker file.
#	   Mark the log at its START barrier and after it acknowledges.
#	3. Replay the log to each recorded FLUSH/FUA between the marks and
#	   to the ACK mark; import a copy of each state and verify it.
#	4. Require that no TXG synced between START and the clearing: the
#	   publication was still only in the log.
#

verify_runnable "global"

log_assert "O_TMPFILE publication crash states: punchmark_indirect"
log_onexit tmpfile_dmlog_cleanup

DMLOG_EXPECT_RECORD="TX_TMPFILE"
DMLOG_FORBID_RECORD="TX_WRITE2 TX_TRUNCATE"
DMLOG_REQUIRE=PUNCHED
tmpfile_dmlog punchmark sa 0

typeset start punched
start=$(sed -n 's/^START txg=\([0-9]*\).*/\1/p' $DMLOG_DIR/run.out)
punched=$(sed -n 's/^PUNCHED txg=\([0-9]*\).*/\1/p' $DMLOG_DIR/run.out)
[[ -n "$start" && "$start" == "$punched" ]] || \
    log_fail "TXG $start at START, $punched at the clearing: not unsynced"

log_pass "O_TMPFILE publication crash states: punchmark_indirect"

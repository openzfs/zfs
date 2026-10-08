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
#	A crash while the intent log is being replayed loses nothing: two
#	publications (the second empty) and a synced marker, each replay
#	resuming through a read-only mount of the file system, after which
#	the files are removed. The resumed replay recovers exactly what an
#	uninterrupted one does.
#
# STRATEGY:
#	1. Record the "multi" scenario of tmpfile_crash on a dm-log-writes
#	   device and rebuild its state at ACK.
#	2. Import that state on a second dm-log-writes device, with each
#	   replayed record synced in its own TXG, recording the replay.
#	3. Rebuild the state left by a crash at a bounded set of points of
#	   that replay; import each (resuming the replay, read-only) and
#	   compare it with the uninterrupted replay.
#

verify_runnable "global"

log_assert "O_TMPFILE publication, crash during replay: multi_ro"
log_onexit tmpfile_dmlog_cleanup

tmpfile_dmlog_replay multi 24 ro

log_pass "O_TMPFILE publication, crash during replay: multi_ro"

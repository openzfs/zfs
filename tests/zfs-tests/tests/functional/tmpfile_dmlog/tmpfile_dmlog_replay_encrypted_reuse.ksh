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
#	A crash while the intent log of an encrypted file system is being
#	replayed loses nothing, with the key loaded by the import: a publication
#	reusing the object number of a removed one, so that a resumed replay
#	adopts an object.  The resumed replay recovers exactly what an
#	uninterrupted one does.
#
# STRATEGY:
#	1. Record the "reuse" scenario of tmpfile_crash on a dm-log-writes
#	   device, with an encrypted file system, and rebuild its state at
#	   ACK.
#	2. Import that state on a second dm-log-writes device, with each
#	   replayed record synced in its own TXG, recording the replay.
#	3. Rebuild the state left by a crash at a bounded set of points of
#	   that replay; import each (resuming the replay) and
#	   compare it with the uninterrupted replay.
#

verify_runnable "global"

log_assert "O_TMPFILE publication, crash during replay: replay_encrypted_reuse"
log_onexit tmpfile_dmlog_cleanup

tmpfile_dmlog_encrypt
DMLOG_KEY_AT_IMPORT=1
DMLOG_FREEZE=1
DMLOG_REQUIRE=REUSE
tmpfile_dmlog_replay reuse 24

log_pass "O_TMPFILE publication, crash during replay: replay_encrypted_reuse"

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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 by ConnectWise. All rights reserved.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that zstream dump rejects a send stream whose first compressed
# DRR_WRITE has logical_size below SPA_MINBLOCKSIZE.
#
# Strategy:
# 1. Load the prebuilt mutated stream
#    (undersized-write-lsize.zsend.bz2)
# 2. Confirm zstream dump fails with the shared recv_check
#    "logical size ... below minimum" message
#

verify_runnable "both"

log_assert "undersized WRITE logical_size is rejected by zstream dump"
log_onexit cleanup_pool $POOL

typeset src="$ZSTREAM_DATADIR/undersized-write-lsize.zsend.bz2"
typeset bad=$BACKDIR/bad
typeset err=$BACKDIR/err

log_must eval "bzcat <$src >$bad"
log_mustnot eval "zstream dump $bad >$err 2>&1"
log_must eval "grep -E -q 'DRR_WRITE logical size .* below minimum' $err"
log_note "dump rejected: $(head -1 $err)"

log_pass "undersized WRITE logical_size is rejected by zstream dump"

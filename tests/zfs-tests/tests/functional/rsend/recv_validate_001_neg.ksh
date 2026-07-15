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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that zfs recv rejects a send stream whose first compressed
# DRR_WRITE has logical_size below SPA_MINBLOCKSIZE.
#
# Strategy:
# 1. Load the prebuilt mutated stream from the zstream testdata
#    (undersized-write-lsize.zsend.bz2)
# 2. Confirm zfs recv fails with the shared recv_check
#    "logical size ... below minimum" message
#

verify_runnable "both"

function cleanup
{
	datasetexists $badrecv && destroy_dataset $badrecv -r
	[[ -f $bad ]] && rm -f $bad
	[[ -f $err ]] && rm -f $err
}
log_onexit cleanup

log_assert "undersized WRITE logical_size is rejected by zfs recv"

typeset src=$STF_SUITE/tests/functional/zstream/undersized-write-lsize.zsend.bz2
typeset bad=$BACKDIR/undersized-write-lsize.zsend
typeset err=$BACKDIR/undersized-write-lsize.err
typeset badrecv=$POOL/badrecv

log_must mkdir -p $BACKDIR
log_must eval "bzcat <$src >$bad"
datasetexists $badrecv && log_must zfs destroy -r $badrecv
log_mustnot eval "zfs recv $badrecv < $bad >$err 2>&1"
log_must eval "grep -E -q 'DRR_WRITE logical size .* below minimum' $err"
log_note "recv rejected: $(head -1 $err)"
datasetexists $badrecv && log_must zfs destroy -r $badrecv

log_pass "undersized WRITE logical_size is rejected by zfs recv"

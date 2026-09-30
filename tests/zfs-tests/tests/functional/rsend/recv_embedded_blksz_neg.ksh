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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that zfs recv rejects a DRR_WRITE_EMBEDDED record whose logical
# size does not equal its length.
#
# The sender always sets a DRR_WRITE_EMBEDDED record's drr_length to the
# embedded block pointer's logical size (dump_write_embedded()), so the two
# are always equal in a genuine stream.  A record where they differ can only
# come from a corrupt or hand-crafted stream.  If it were received it would
# store a block pointer whose LSIZE does not match the object's block size,
# which silently loses the rest of the block and later trips assertions when
# the received snapshot is sent again.  recv_check_drr_write_embedded() must
# reject it.
#
# Strategy:
# 1. Load a prebuilt stream whose first DRR_WRITE_EMBEDDED record has
#    drr_lsize (4096) != drr_length (8192) and whose DRR_OBJECT gives that
#    object an 8192-byte block size (embedded-blksz-mismatch.zsend.bz2).
# 2. Confirm zfs recv fails with the shared recv_check "logical size ...
#    must equal length" message.
# 3. Confirm no dataset is left behind.
#

verify_runnable "both"

typeset src=$STF_SUITE/tests/functional/rsend/embedded-blksz-mismatch.zsend.bz2
typeset bad=$BACKDIR/embedded-blksz-mismatch.zsend
typeset err=$BACKDIR/embedded-blksz-mismatch.err
typeset badrecv=$POOL/badrecv

function cleanup
{
	datasetexists $badrecv && destroy_dataset $badrecv -r
	rm -f $bad $err
}
log_onexit cleanup

log_assert "a DRR_WRITE_EMBEDDED record with lsize != length is rejected"

log_must mkdir -p $BACKDIR
log_must eval "bzcat <$src >$bad"
datasetexists $badrecv && log_must zfs destroy -r $badrecv
log_mustnot eval "zfs recv $badrecv <$bad >$err 2>&1"
log_must eval "grep -E -q 'DRR_WRITE_EMBEDDED logical size .* must equal length' $err"
log_note "recv rejected: $(head -1 $err)"
log_mustnot datasetexists $badrecv

log_pass "a DRR_WRITE_EMBEDDED record with lsize != length is rejected"

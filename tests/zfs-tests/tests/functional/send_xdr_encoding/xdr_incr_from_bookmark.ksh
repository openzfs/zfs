#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

. $STF_SUITE/tests/functional/send_xdr_encoding/send_xdr_encoding.kshlib

#
# Description:
# An incremental send from a redaction bookmark (zfs send -i ds#book ds@snap)
# carries BEGINNV_REDACT_FROM_SNAPS in its DRR_BEGIN nvlist payload (via the
# from_rl path). Verify that this payload is XDR-encoded and the stream can
# be received.
#
# Strategy:
# 1. Create a source dataset with a redaction bookmark.
# 2. Send a redacted full stream from that bookmark's source snapshot
#    and receive it into a second pool as a base.
# 3. Add data and a new snapshot on the source.
# 4. zfs send -i sendfs#redaction-bookmark sendfs@snap to a file.
# 5. Verify XDR encoding in the resulting stream.
# 6. Verify that zfs receive of the stream succeeds.
#

verify_runnable "both"

sendfs="$POOL/xdr_incr_from_bookmark_src"
clonefs="$POOL/xdr_incr_from_bookmark_clone"
recvfs="$POOL2/xdr_incr_from_bookmark_recv"
full_stream="/$POOL/xdr_incr_from_bookmark_full.zsend"
incr_stream="/$POOL/xdr_incr_from_bookmark_incr.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $full_stream $incr_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of an incremental send from a redaction bookmark " \
    "is XDR-encoded and receivable"

log_must zfs create $sendfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must dd if=/dev/urandom of=/$sendfs/f2 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s0

log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=8 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

# Establish a base on the receiver.
log_must eval "zfs send --redact redaction-bookmark $sendfs@s0 > $full_stream"
log_must eval "zfs receive $recvfs < $full_stream"

# Add a new snapshot on the source for the incremental.
log_must dd if=/dev/urandom of=/$sendfs/f3 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s1

# Generate an incremental send from the redaction bookmark. This fires
# BEGINNV_REDACT_FROM_SNAPS via the from_rl path because the from-side
# is a redaction bookmark.
log_must eval "zfs send -i $sendfs#redaction-bookmark $sendfs@s1 > $incr_stream"
verify_xdr_nvlist_encoding $incr_stream
log_must eval "zfs receive $recvfs < $incr_stream"

log_pass "BEGIN nvlist of an incremental send from a redaction bookmark " \
    "is XDR-encoded and receivable"

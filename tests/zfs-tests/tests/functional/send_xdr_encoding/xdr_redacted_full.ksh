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
# A redacted send (zfs send --redact <bookmark>) carries BEGINNV_REDACT_SNAPS
# in its DRR_BEGIN nvlist payload. Verify that this payload is XDR-encoded and
# the stream can be received.
#
# Strategy:
# 1. Create a source dataset and a divergent clone.
# 2. Create a redaction bookmark on the source snapshot relative to the
#    clone snapshot.
# 3. zfs send --redact <bookmark> sendfs@snap to a file.
# 4. verify_xdr_nvlist_encoding on the stream.
# 5. Verify that zfs receive succeeds.
#

verify_runnable "both"

sendfs="$POOL/xdr_redacted_full_src"
clonefs="$POOL/xdr_redacted_full_clone"
recvfs="$POOL2/xdr_redacted_full_recv"
stream="/$POOL/xdr_redacted_full.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a redacted send is XDR-encoded and receivable"

log_must zfs create $sendfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must dd if=/dev/urandom of=/$sendfs/f2 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s0

log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=8 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

log_must eval "zfs send --redact redaction-bookmark $sendfs@s0 > $stream"
verify_xdr_nvlist_encoding $stream
log_must eval "zfs receive $recvfs < $stream"

log_pass "BEGIN nvlist of a redacted send is XDR-encoded and receivable"

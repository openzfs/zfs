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
# Sending a snapshot from a previously-redacted dataset (one with the
# SPA_FEATURE_REDACTED_DATASETS feature active, e.g., one that was received
# from a redacted send) carries BEGINNV_REDACT_SNAPS in its DRR_BEGIN
# nvlist payload via a different code path than the producer-side --redact
# flag. Verify that this payload is XDR-encoded and that the stream can be
# received.
#
# Strategy:
# 1. Produce a redacted dataset on a receiver via a redacted full send.
# 2. zfs send the received-redacted snapshot to a new dataset.
# 3. Verify XDR encoding on the new stream.
# 4. Verify that a zfs receive of the new stream succeeds.
#

verify_runnable "both"

sendfs="$POOL/xdr_redacted_received_src"
clonefs="$POOL/xdr_redacted_received_clone"
midfs="$POOL2/xdr_redacted_received_mid"
recvfs="$POOL2/xdr_redacted_received_recv"
full_stream="/$POOL/xdr_redacted_received_full.zsend"
resend_stream="/$POOL/xdr_redacted_received_resend.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $midfs && destroy_dataset $midfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $full_stream $resend_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a send from a previously-redacted dataset is " \
    "XDR-encoded and receivable"

log_must zfs create $sendfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must dd if=/dev/urandom of=/$sendfs/f2 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s0

log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=8 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

# Produce a previously-redacted dataset on the receiver.
log_must eval "zfs send --redact redaction-bookmark $sendfs@s0 > $full_stream"
log_must eval "zfs receive $midfs < $full_stream"

# Send the received-redacted snapshot. This fires BEGINNV_REDACT_SNAPS via
# the SPA_FEATURE_REDACTED_DATASETS code path on to_ds.
log_must eval "zfs send $midfs@s0 > $resend_stream"
verify_xdr_nvlist_encoding $resend_stream
log_must eval "zfs receive $recvfs < $resend_stream"

log_pass "BEGIN nvlist of a send from a previously-redacted dataset is " \
    "XDR-encoded and receivable"

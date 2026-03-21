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
# zfs send explicitly disallows the source-side combination of -w and
# --redact. However, the same nvlist combination (BEGINNV_REDACT_SNAPS
# together with crypt_keydata) can still be reached by:
#
#   1. Sending a redacted (non-raw) stream from an unencrypted source.
#   2. Receiving it with receiver-side encryption.
#   3. Re-sending the now-encrypted-and-redacted dataset with -w.
#
# The final stream's DRR_BEGIN nvlist contains both the redact-snaps array
# (via the SPA_FEATURE_REDACTED_DATASETS code path on to_ds) and
# crypt_keydata (via DMU_BACKUP_FEATURE_RAW). Verify that this combined
# payload is XDR-encoded and that the stream can be received.
#
# Strategy:
# 1. Create an unencrypted source dataset with a redaction bookmark.
# 2. zfs send --redact <book> sendfs@snap to a file (no -w).
# 3. zfs receive into a new dataset with -o encryption=on (receiver-side
#    encryption).
# 4. zfs send -w the received dataset to a second stream file.
# 5. Verify that this second stream is XDR-encoded.
# 6. Verify that the second stream can be zfs received successfully.
#

verify_runnable "both"

sendfs="$POOL/xdr_redacted_received_raw_src"
clonefs="$POOL/xdr_redacted_received_raw_clone"
midfs="$POOL2/xdr_redacted_received_raw_mid"
recvfs="$POOL2/xdr_redacted_received_raw_recv"
keyfile="/$POOL/xdr_redacted_received_raw.key"
full_stream="/$POOL/xdr_redacted_received_raw_full.zsend"
resend_stream="/$POOL/xdr_redacted_received_raw_resend.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $midfs && destroy_dataset $midfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $keyfile $full_stream $resend_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a raw send of a received-redacted dataset is " \
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

# Redacted send (non-raw) into a receiver that establishes its own encryption.
log_must eval "zfs send --redact redaction-bookmark $sendfs@s0 > $full_stream"
log_must eval "echo 'thisisapassphrase' > $keyfile"
log_must eval "zfs receive -o encryption=on -o keyformat=passphrase " \
    "-o keylocation=file://$keyfile $midfs < $full_stream"

# Re-send the received stream as a raw (encrypted) stream. The DRR_BEGIN
# nvlist now carries both BEGINNV_REDACT_SNAPS data and crypt_keydata
# (DMU_BACKUP_FEATURE_RAW).
log_must eval "zfs send -w $midfs@s0 > $resend_stream"
verify_xdr_nvlist_encoding $resend_stream
log_must eval "zfs receive $recvfs < $resend_stream"

log_pass "BEGIN nvlist of a raw send of a received-redacted dataset is " \
    "XDR-encoded and receivable"

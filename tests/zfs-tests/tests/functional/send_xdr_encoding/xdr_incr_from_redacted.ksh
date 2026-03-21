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
# An incremental send whose from-side is a snapshot of a previously-redacted
# dataset carries BEGINNV_REDACT_FROM_SNAPS in its DRR_BEGIN nvlist payload
# via a different code path than incrementals from a redaction bookmark
# (the dspp->numfromredactsnaps path). Verify that this payload is
# XDR-encoded and that the stream can be received.
#
# Strategy:
# 1. Produce a redacted dataset on a receiver via a redacted full send,
#    leaving the receiver with a snapshot whose from-side will carry the
#    SPA_FEATURE_REDACTED_DATASETS feature.
# 2. Establish the same base on a tertiary destination so we have somewhere
#    to apply the incremental.
# 3. Create a new snapshot of the receiver-side redacted dataset.
# 4. zfs send -i mid@s0 mid@s1 to a file.
# 5. Verify that the stream is XDR encoded.
# 6. Verify that we can zfs receive the incremental onto the tertiary base.
#

verify_runnable "both"

sendfs="$POOL/xdr_incr_from_redacted_src"
clonefs="$POOL/xdr_incr_from_redacted_clone"
midfs="$POOL2/xdr_incr_from_redacted_mid"
tertiary="$POOL/xdr_incr_from_redacted_tertiary"
full_stream="/$POOL/xdr_incr_from_redacted_full.zsend"
incr_stream="/$POOL/xdr_incr_from_redacted_incr.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $midfs && destroy_dataset $midfs -R
	datasetexists $tertiary && destroy_dataset $tertiary -R
	rm -f $full_stream $incr_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of an incremental from a previously-redacted " \
    "snapshot is XDR-encoded and receivable"

log_must zfs create $sendfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must dd if=/dev/urandom of=/$sendfs/f2 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s0

log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=8 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

# Produce two receivers of the redacted full send: one we will re-send from
# (mid) and one we will receive the incremental into (tertiary).
log_must eval "zfs send --redact redaction-bookmark $sendfs@s0 > $full_stream"
log_must eval "zfs receive $midfs < $full_stream"
log_must eval "zfs receive $tertiary < $full_stream"

# Create a fresh snapshot of the redacted receiver. The data has not changed
# (and cannot be modified without mounting), but the snapshot itself is
# enough to drive an incremental send and trigger the case-4 nvlist path.
log_must zfs snapshot $midfs@s1

# Create an incremental send from the redacted from-side. This fires
# BEGINNV_REDACT_FROM_SNAPS via the dspp->numfromredactsnaps path because
# $midfs@s0 has the SPA_FEATURE_REDACTED_DATASETS feature active.
log_must eval "zfs send -i $midfs@s0 $midfs@s1 > $incr_stream"
verify_xdr_nvlist_encoding $incr_stream
log_must eval "zfs receive $tertiary < $incr_stream"

log_pass "BEGIN nvlist of an incremental from a previously-redacted snapshot " \
    "is XDR-encoded and receivable"

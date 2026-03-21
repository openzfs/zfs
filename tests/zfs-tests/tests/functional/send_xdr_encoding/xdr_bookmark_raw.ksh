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
# A raw incremental send from a redaction bookmark on an encrypted dataset
# (zfs send -w -i ds#book ds@snap) carries both BEGINNV_REDACT_FROM_SNAPS
# and crypt_keydata in its DRR_BEGIN nvlist payload. Verify that this
# combined payload is XDR-encoded and the stream can be received.
#
# Strategy:
# 1. Create an encrypted source dataset with a redaction bookmark and a
#    later snapshot.
# 2. Establish a raw base on the receiver via zfs send -w of the bookmark's
#    source snapshot.
# 3. zfs send -w -i sendfs#book sendfs@s1 to a file.
# 4. Verify that the resulting stream is XDR-encoded.
# 5. Verify that the zfs receive succeeds.
#

verify_runnable "both"

sendfs="$POOL/xdr_bookmark_raw_src"
clonefs="$POOL/xdr_bookmark_raw_clone"
recvfs="$POOL2/xdr_bookmark_raw_recv"
keyfile="/$POOL/xdr_bookmark_raw.key"
full_stream="/$POOL/xdr_bookmark_raw_full.zsend"
incr_stream="/$POOL/xdr_bookmark_raw_incr.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $keyfile $full_stream $incr_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a raw incremental from a redaction bookmark is " \
    "XDR-encoded and receivable"

log_must eval "echo 'thisisapassphrase' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $sendfs

log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must dd if=/dev/urandom of=/$sendfs/f2 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s0

# The clone inherits encryption from $sendfs.
log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=8 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

# Take @s1 with no intervening writes. See xdr_bookmark_raw_with_write.ksh
# for a variant that includes a post-redact write; that variant exercises
# a known kernel-side issue (#18491) and may flake.
log_must zfs snapshot $sendfs@s1

# Establish a raw base on the receiver.
log_must eval "zfs send -w $sendfs@s0 > $full_stream"
log_must eval "zfs receive $recvfs < $full_stream"

# Raw incremental from the redaction bookmark. This is the test focus.
log_must eval "zfs send -w -i $sendfs#redaction-bookmark $sendfs@s1 > \
    $incr_stream"
verify_xdr_nvlist_encoding $incr_stream
log_must eval "zfs receive $recvfs < $incr_stream"

log_pass "BEGIN nvlist of a raw incremental from a redaction bookmark is " \
    "XDR-encoded and receivable"

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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/tests/functional/send_xdr_encoding/send_xdr_encoding.kshlib

#
# Description:
# The most populated DRR_BEGIN nvlist in the kernel: a token-resumed raw
# incremental from a redaction bookmark carries BEGINNV_REDACT_FROM_SNAPS,
# crypt_keydata, and BEGINNV_RESUME_{OBJECT,OFFSET}. Verify that this
# combined payload is XDR-encoded and the resumed stream can be received.
#
# Strategy:
# 1. Create an encrypted source with a redaction bookmark and a later
#    snapshot, mirroring xdr_bookmark_raw.
# 2. Establish a raw base on the receiver.
# 3. zfs send -w -i sendfs#book sendfs@s1 to a file, truncate it, then
#    attempt receive so that a resume token is left behind.
# 4. zfs send -t <token> to produce the resumed stream.
# 5. Verify that the resumed stream is XDR-encoded.
# 6. Verify that zfs receive -s of the resumed stream is successful.
#

verify_runnable "both"

sendfs="$POOL/xdr_resume_bookmark_raw_src"
clonefs="$POOL/xdr_resume_bookmark_raw_clone"
recvfs="$POOL2/xdr_resume_bookmark_raw_recv"
keyfile="/$POOL/xdr_resume_bookmark_raw.key"
full_stream="/$POOL/xdr_resume_bookmark_raw_full.zsend"
incr_stream="/$POOL/xdr_resume_bookmark_raw_incr.zsend"
resumed_stream="/$POOL/xdr_resume_bookmark_raw_resumed.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $keyfile $full_stream $incr_stream $resumed_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a token-resumed raw incremental from a redaction " \
    "bookmark is XDR-encoded and receivable"

log_must eval "echo 'thisisapassphrase' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $sendfs

log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=16 status=none
log_must dd if=/dev/urandom of=/$sendfs/f2 bs=128k count=16 status=none
log_must zfs snapshot $sendfs@s0

log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=16 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

# Take @s1 with no intervening write. See xdr_resume_bookmark_raw_with_write.ksh
# for a variant that includes a post-redact write; that variant exercises
# a known kernel-side issue (#18491) and may flake.
log_must zfs snapshot $sendfs@s1

# Establish a raw base on the receiver.
log_must eval "zfs send -w $sendfs@s0 > $full_stream"
log_must eval "zfs receive $recvfs < $full_stream"

# Truncate-and-resume on the raw incremental from the redaction bookmark.
log_must eval "zfs send -w -i $sendfs#redaction-bookmark $sendfs@s1 > \
    $incr_stream"
mess_send_file $incr_stream
log_mustnot eval "zfs receive -s $recvfs < $incr_stream"

token=$(get_prop receive_resume_token $recvfs)
[[ -n "$token" && "$token" != "-" ]] || \
    log_fail "no resume token left behind by partial receive"
log_must eval "zfs send -t $token > $resumed_stream"

verify_xdr_nvlist_encoding $resumed_stream
log_must eval "zfs receive -s $recvfs < $resumed_stream"

log_pass "BEGIN nvlist of a token-resumed raw incremental from a redaction " \
    "bookmark is XDR-encoded and receivable"

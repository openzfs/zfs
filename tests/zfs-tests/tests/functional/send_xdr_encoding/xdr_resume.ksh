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
# A token-resumed send (zfs send -t <token>) carries BEGINNV_RESUME_OBJECT
# and BEGINNV_RESUME_OFFSET in its DRR_BEGIN nvlist payload. Verify that
# this payload is XDR-encoded and that the resumed stream can be received.
#
# Strategy:
# 1. Create a small dataset with one snapshot.
# 2. zfs send the snapshot to a file, truncate it, then attempt receive
#    so that a resume token is left behind.
# 3. zfs send -t <token> to produce the resumed stream.
# 4. Verify that the resumed stream is XDR-encoded.
# 5. Verify that zfs receive -s on the resumed stream is successful.
#

verify_runnable "both"

sendfs="$POOL/xdr_resume_src"
recvfs="$POOL2/xdr_resume_recv"
full_stream="/$POOL/xdr_resume_full.zsend"
resumed_stream="/$POOL/xdr_resume_resumed.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -r
	datasetexists $recvfs && destroy_dataset $recvfs -r
	rm -f $full_stream $resumed_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a token-resumed send is XDR-encoded and receivable"

log_must zfs create $sendfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s1

log_must eval "zfs send $sendfs@s1 > $full_stream"
mess_send_file $full_stream
log_mustnot eval "zfs receive -s $recvfs < $full_stream"

token=$(get_prop receive_resume_token $recvfs)
[[ -n "$token" && "$token" != "-" ]] || \
    log_fail "no resume token left behind by partial receive"
log_must eval "zfs send -t $token > $resumed_stream"

verify_xdr_nvlist_encoding $resumed_stream
log_must eval "zfs receive -s $recvfs < $resumed_stream"

log_pass "BEGIN nvlist of a token-resumed send is XDR-encoded and receivable"

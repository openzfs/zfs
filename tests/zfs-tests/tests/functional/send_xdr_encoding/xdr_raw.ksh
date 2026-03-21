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
# A raw send of an encrypted dataset (zfs send -w) carries a "crypt_keydata"
# nested nvlist in its DRR_BEGIN nvlist payload. Verify that this payload is
# XDR-encoded and that the stream can be received.
#
# Strategy:
# 1. Create an encrypted dataset with one snapshot.
# 2. zfs send -w to a file.
# 3. Verify that the stream is XDR-encoded.
# 4. Verify that zfs receive succeeds.
#

verify_runnable "both"

sendfs="$POOL/xdr_raw_src"
recvfs="$POOL2/xdr_raw_recv"
keyfile="/$POOL/xdr_raw.key"
stream="/$POOL/xdr_raw.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -r
	datasetexists $recvfs && destroy_dataset $recvfs -r
	rm -f $keyfile $stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a raw send of an encrypted dataset is " \
    "XDR-encoded and receivable"

log_must eval "echo 'thisisapassphrase' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $sendfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s1

log_must eval "zfs send -w $sendfs@s1 > $stream"

verify_xdr_nvlist_encoding $stream
log_must eval "zfs receive $recvfs < $stream"

log_pass "BEGIN nvlist of a raw send of an encrypted dataset is " \
    "XDR-encoded and receivable"

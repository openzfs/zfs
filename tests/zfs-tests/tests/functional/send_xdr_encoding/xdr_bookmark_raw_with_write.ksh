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

#
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

. $STF_SUITE/tests/functional/send_xdr_encoding/send_xdr_encoding.kshlib

#
# Description:
# This is the post-redact-write variant of xdr_bookmark_raw, which also
# exports and imports the pool before the incremental send (openzfs/zfs#18491).
#
# A raw send from a redaction bookmark looks up the block pointers of the
# blocks the bookmark redacted (PREVIOUSLY_REDACTED in send_reader_thread()).
# Those lookups must not need the dataset's key: the send is raw, and the
# snapshot is held without a key mapping.  On a warm pool whether they did
# depended on the ARC: if a decrypted copy of the snapshot's dnode block was
# cached (from the mounted head dataset), the lookup succeeded; if not, it
# failed with EACCES ("dataset key must be loaded").  Exporting and
# importing the pool without loading the key leaves no decrypted copy, so
# the lookup takes the decrypting path every time.
#

verify_runnable "both"

sendfs="$POOL/xdr_bookmark_raw_with_write_src"
clonefs="$POOL/xdr_bookmark_raw_with_write_clone"
recvfs="$POOL2/xdr_bookmark_raw_with_write_recv"
keyfile="/$POOL/xdr_bookmark_raw_with_write.key"
full_stream="/$POOL/xdr_bookmark_raw_with_write_full.zsend"
incr_stream="/$POOL/xdr_bookmark_raw_with_write_incr.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $keyfile $full_stream $incr_stream
}
log_onexit cleanup

log_assert "BEGIN nvlist of a raw incremental from a redaction bookmark, " \
    "with a post-redact write, is XDR-encoded and receivable"

log_must eval "echo 'thisisapassphrase' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $sendfs

log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s0

# The clone inherits encryption from $sendfs.
log_must zfs clone $sendfs@s0 $clonefs
log_must dd if=/dev/urandom of=/$clonefs/f1 bs=128k count=8 conv=notrunc \
    status=none
log_must zfs snapshot $clonefs@s

log_must zfs redact $sendfs@s0 redaction-bookmark $clonefs@s

# Post-redact write.
log_must dd if=/dev/urandom of=/$sendfs/f3 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s1

# Establish a raw base on the receiver.
log_must eval "zfs send -w $sendfs@s0 > $full_stream"
log_must eval "zfs receive $recvfs < $full_stream"

# Drop every cached copy of the source's blocks; the key stays unloaded,
# since a raw send must not need it.
log_must zpool export $POOL
log_must zpool import $POOL
log_must eval "[[ \"$(get_prop keystatus $sendfs)\" == unavailable ]]"

log_must eval "zfs send -w -i $sendfs#redaction-bookmark $sendfs@s1 > \
    $incr_stream"
verify_xdr_nvlist_encoding $incr_stream
log_must eval "zfs receive $recvfs < $incr_stream"

log_pass "BEGIN nvlist of a raw incremental from a redaction bookmark, " \
    "with a post-redact write, is XDR-encoded and receivable"

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
# A replication send (zfs send -R) may emit two distinct categories of
# DRR_BEGIN record:
#
#   1. A wrapper BEGIN of type DMU_COMPOUNDSTREAM, generated in libzfs
#      (lib/libzfs/libzfs_sendrecv.c), whose nvlist describes the package
#      stream. This BEGIN has always been XDR-encoded and is not affected
#      by the kernel-side encoding changes introduced in PR #18372.
#
#   2. One inner BEGIN record per dataset whose contents are included, 
#      generated in the kernel (module/zfs/dmu_send.c). These are the BEGIN
#      records whose encoding the kernel-side change consolidates to XDR.
#
# All other tests in this suite exercise category (2). This test exercises
# both categories together: it verifies that no BEGIN record produced
# anywhere on the userspace+kernel send path is encoded with NV_ENCODE_NATIVE,
# so a future regression in either layer would be caught.
#
# Strategy:
# 1. Create an unencrypted parent dataset and an encrypted child filesystem
#    underneath it, with some data in each. The encrypted child is what
#    causes the kernel-side inner BEGIN to actually carry an nvlist payload
#    (crypt_keydata) rather than passing through silently.
# 2. Snapshot recursively.
# 3. zfs send -wR parent@snap to a file. The resulting stream contains a
#    libzfs-generated wrapper BEGIN with its compound-stream nvlist plus
#    one kernel-generated inner BEGIN per dataset; the child's inner BEGIN
#    carries crypt_keydata.
# 4. Verify the encoding for the whole stream — this checks every BEGIN
#    nvlist line that zstream dump emits, so it covers both the wrapper
#    and the encrypted child's inner record.
# 5. Verify that the stream can be zfs received successfully.
#

verify_runnable "both"

sendfs="$POOL/xdr_replication_src"
childfs="$POOL/xdr_replication_src/child"
recvfs="$POOL2/xdr_replication_recv"
keyfile="/$POOL/xdr_replication.key"
stream="/$POOL/xdr_replication.zsend"

function cleanup
{
	datasetexists $sendfs && destroy_dataset $sendfs -R
	datasetexists $recvfs && destroy_dataset $recvfs -R
	rm -f $keyfile $stream
}
log_onexit cleanup

log_assert "BEGIN nvlists in a recursive replication stream (wrapper and inner) are XDR-encoded and receivable"

log_must zfs create $sendfs
log_must eval "echo 'thisisapassphrase' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $childfs
log_must dd if=/dev/urandom of=/$sendfs/f1 bs=128k count=4 status=none
log_must dd if=/dev/urandom of=/$childfs/f1 bs=128k count=4 status=none
log_must zfs snapshot -r $sendfs@s0

log_must eval "zfs send -wR $sendfs@s0 > $stream"
verify_xdr_nvlist_encoding $stream
log_must eval "zfs receive $recvfs < $stream"

log_pass "BEGIN nvlists in a recursive replication stream (wrapper and inner) are XDR-encoded and receivable"


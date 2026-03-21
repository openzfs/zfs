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
# This is the post-redact-write variant of xdr_bookmark_raw, separated out
# because of a known issue (#18491) that causes it to fail roughly 30% of
# the time. It's included here as a test for issue #18491 until the exact
# source of that problem can be pinned down more specifically.
#
# Known issue: openzfs/zfs#18491
#
# On a freshly-created pool, `zfs send -w -i ds#book ds@snap` intermittently
# fails with EACCES whenever there is data-modifying activity between the
# `zfs redact` that created the bookmark and the subsequent send. This EACCES
# is surfaced to userspace as the misleading message "dataset key must be
# loaded," although the key remains loaded throughout.
#
# The reproducer script included in the issue report typically triggers the
# problem within about 10 iterations on a fresh pool. Disk-sync mitigations
# (zpool sync, with or without `-f`, with or without sleep, single or doubled,
# applied at any reasonable point) do not avert the problem. CI runs that
# include the test in this file reproduce the failure regularly (though
# intermittently) across multiple distributions. xdr_resume_bookmark_raw.ksh
# removes the post-redact write (which is not essential to the test) and
# therefore runs reliably.
#
# When this test fails, the failure marker is the libzfs warning
# "dataset key must be loaded" on stderr from the first `zfs send -w -i`
# line below (the one that produces the stream we then truncate), and a
# non-zero exit from that send. The test does not attempt to distinguish
# the known-issue failure from other possible failures.
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
    "with a post-redact write, is XDR-encoded and receivable " \
    "(known to flake; see openzfs/zfs#18491)"

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

# Post-redact write: the trigger for openzfs/zfs#18491.
log_must dd if=/dev/urandom of=/$sendfs/f3 bs=128k count=8 status=none
log_must zfs snapshot $sendfs@s1

# Establish a raw base on the receiver.
log_must eval "zfs send -w $sendfs@s0 > $full_stream"
log_must eval "zfs receive $recvfs < $full_stream"

# The next line is what races. On failure it exits with EACCES rendered
# as "dataset key must be loaded".
log_must eval "zfs send -w -i $sendfs#redaction-bookmark $sendfs@s1 > \
    $incr_stream"
verify_xdr_nvlist_encoding $incr_stream
log_must eval "zfs receive $recvfs < $incr_stream"

log_pass "BEGIN nvlist of a raw incremental from a redaction bookmark, " \
    "with a post-redact write, is XDR-encoded and receivable"

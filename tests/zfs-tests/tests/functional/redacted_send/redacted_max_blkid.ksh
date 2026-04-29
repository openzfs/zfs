#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

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

#
# Copyright (c) 2026 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify that an incremental send from a redaction bookmark correctly sends
# the last block (dn_maxblkid) of a file through the PREVIOUSLY_REDACTED
# path.
#
# Regression test for an off-by-one bug in the PREVIOUSLY_REDACTED handler
# in send_reader_thread().  file_max was computed as:
#
#   MIN(dn->dn_maxblkid, range->end_blkid)
#
# dn_maxblkid is an inclusive maximum block ID while range->end_blkid is
# exclusive (one past the last block).  Mixing these in MIN() caused the
# loop condition "blkid < file_max" to skip block dn_maxblkid, silently
# dropping the last block of any file whose last block was in the redaction
# list.  The block remained as zeros on the receiver even though ZFS
# reported the send and receive as successful.
#
# Strategy:
# 1. Create a dataset with a 16-block file of random data and snapshot it.
# 2. Create a clone (redact_clone) that overwrites only the last block
#    (block 15, i.e., dn_maxblkid).
# 3. Redact the base snapshot using redact_clone; block 15 enters the
#    redaction list.
# 4. Create a second clone (send_clone) of the base snapshot that does NOT
#    modify block 15.  Because block 15 in send_clone has birth <=
#    snap.creation_txg the TO traversal thread skips it; it must be sent
#    via the PREVIOUSLY_REDACTED path.
# 5. Redacted-send the base snapshot to the receiver (block 15 = zeros).
# 6. Incrementally send send_clone from the redaction bookmark; block 15
#    must be filled in by the PREVIOUSLY_REDACTED handler.
# 7. Verify that block 15 on the receiver matches the original.
#

typeset ds_name="max_blkid"
typeset sendfs="$POOL/$ds_name"
typeset redact_clone="$POOL/${ds_name}_redact"
typeset send_clone="$POOL/${ds_name}_send"
typeset recvfs="$POOL2/$ds_name"
typeset recv_clone="$POOL2/${ds_name}_send"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)

log_onexit redacted_cleanup $sendfs $recvfs $recv_clone

# Create a dataset with a 16-block file.
log_must zfs create $sendfs
typeset mntpnt=$(get_prop mountpoint $sendfs)
typeset bs=$(get_prop recsize $sendfs)
log_must dd if=/dev/urandom of=$mntpnt/f1 bs=$bs count=16

# Take the base snapshot.
log_must zfs snapshot $sendfs@snap

# Create redact_clone and overwrite ONLY the last block (block 15).
# This is the block at index dn_maxblkid for a 16-block file.
log_must zfs clone $sendfs@snap $redact_clone
typeset redact_mnt=$(get_prop mountpoint $redact_clone)
log_must dd if=/dev/urandom of=$redact_mnt/f1 bs=$bs count=1 seek=15 conv=notrunc
log_must zfs snapshot $redact_clone@snap

# Create the redaction bookmark; block 15 is now in the redaction list.
log_must zfs redact $sendfs@snap book1 $redact_clone@snap

# Create send_clone as an unmodified clone of the base snapshot.
# Block 15 in send_clone is inherited (birth <= snap.creation_txg), so the
# TO traversal thread does not include it.  The PREVIOUSLY_REDACTED path
# must send it.
log_must zfs clone $sendfs@snap $send_clone
log_must zfs snapshot $send_clone@snap

# Redacted send of the base snapshot; block 15 of f1 is omitted.
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"

# Incremental send of send_clone from the redaction bookmark.
# Block 15 must be sent via the PREVIOUSLY_REDACTED path.
log_must eval "zfs send -i $sendfs#book1 $send_clone@snap >$stream"
log_must eval "zfs recv $recv_clone <$stream"

# Verify that the received clone is identical to the source.
# If the bug is present, block 15 is zeros on the receiver and this fails.
typeset send_mnt=$(get_prop mountpoint $send_clone)
typeset recv_mnt=$(get_prop mountpoint $recv_clone)
log_must directory_diff $send_mnt $recv_mnt

# Explicitly verify block 15 is not all zeros and matches the source.
typeset src_block=$(mktemp $tmpdir/src_block.XXXX)
typeset recv_block=$(mktemp $tmpdir/recv_block.XXXX)
typeset zero_block=$(mktemp $tmpdir/zero_block.XXXX)
log_must dd if=$mntpnt/f1 bs=$bs skip=15 count=1 of=$src_block 2>/dev/null
log_must dd if=$recv_mnt/f1 bs=$bs skip=15 count=1 of=$recv_block 2>/dev/null
log_must dd if=/dev/zero bs=$bs count=1 of=$zero_block 2>/dev/null

cmp -s $recv_block $zero_block && log_fail "Block 15 is all zeros on receiver (off-by-one bug)"
log_must cmp $src_block $recv_block

log_pass "Incremental send from bookmark correctly sends the last block (dn_maxblkid)."

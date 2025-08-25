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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify redaction works as expected for various scenarios.
#
# Strategy:
# 1. An unmodified file does not get redacted at all.
# 2. Empty redaction list redacts everything.
# 3. A file removed in the clone redacts the whole file.
# 4. A file moved in the clone does not redact the file.
# 5. A copied, then removed file in the clone redacts the whole file.
# 6. Overwriting a file with identical contents redacts the file.
# 7. A partially modified block redacts the entire block.
# 8. Only overlapping areas of modified ranges are redacted.
# 9. Send from the root dataset of a pool work correctly.
#

typeset ds_name="contents"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name ''
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"

log_onexit redacted_cleanup $sendfs $recvfs

# An unmodified file does not get redacted at all.
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book1 $clone@snap1
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must mount_redacted -f $recvfs
log_must diff $send_mnt/f1 $recv_mnt/f1
log_must diff $send_mnt/f2 $recv_mnt/f2
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Removing a file in the clone redacts the entire file.
log_must rm "$clone_mnt/f1"
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book3 $clone@snap1
log_must eval "zfs send --redact book3 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE0"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Moving a file in the clone does not redact the file.
log_must mv "$clone_mnt/f1" "$clone_mnt/f1.moved"
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book4 $clone@snap1
log_must eval "zfs send --redact book4 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must mount_redacted -f $recvfs
[[ -f $recv_mnt/f1.moved ]] && log_fail "Found moved file in redacted receive."
log_must diff $send_mnt/f1 $recv_mnt/f1
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Copying, then removing a file in the clone does redact the file.
log_must cp "$clone_mnt/f1" "$clone_mnt/f1.copied"
log_must rm "$clone_mnt/f1"
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book5 $clone@snap1
log_must eval "zfs send --redact book5 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE0"
log_must mount_redacted -f $recvfs
[[ -f $recv_mnt/f1.copied ]] && log_fail "Found moved file in redacted receive."
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Overwriting the contents of a block with identical contents redacts the file.
log_must cp "$clone_mnt/f1" "$clone_mnt/f1.copied"
log_must cp "$clone_mnt/f1.copied" "$clone_mnt/f1"
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book6 $clone@snap1
log_must eval "zfs send --redact book6 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE0"
log_must mount_redacted -f $recvfs
[[ -f $recv_mnt/f1.copied ]] && log_fail "Found moved file in redacted receive."
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Modifying some of a block redacts the whole block.
log_must dd if=/dev/urandom of=$clone_mnt/f1 conv=notrunc seek=2 count=1 bs=32k
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book7 $clone@snap1
log_must eval "zfs send --redact book7 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE1"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Only overlapping areas of modified ranges are redacted.
log_must dd if=/dev/urandom of=$clone_mnt/f2 bs=1024k count=3 conv=notrunc
log_must zfs snapshot $clone@snap1
log_must zfs clone $sendfs@snap $clone/new
typeset mntpnt="$(get_prop mountpoint $clone/new)"
log_must dd if=/dev/urandom of=$mntpnt/f2 bs=1024k seek=1 count=3 \
    conv=notrunc
log_must zfs snapshot $clone/new@snap
log_must zfs redact $sendfs@snap book8 $clone@snap1 $clone/new@snap
log_must eval "zfs send --redact book8 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f2" "$RANGE2"
log_must zfs destroy -R $clone/new
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# FizzBuzz version
log_must zfs clone $sendfs@snap $POOL/stride3
mntpnt="$(get_prop mountpoint $POOL/stride3)"
log_must stride_dd -i /dev/urandom -o $mntpnt/f2 -b $((128 * 1024)) -c 11 -s 3
log_must zfs snapshot $POOL/stride3@snap
log_must zfs clone $sendfs@snap $POOL/stride5
mntpnt="$(get_prop mountpoint $POOL/stride5)"
log_must stride_dd -i /dev/urandom -o $mntpnt/f2 -b $((128 * 1024)) -c 7 -s 5
log_must zfs snapshot $POOL/stride5@snap
log_must zfs redact $sendfs@snap book8a $POOL/stride3@snap $POOL/stride5@snap
log_must eval "zfs send --redact book8a $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f2" "$RANGE3"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Send from the root dataset of a pool work correctly.
log_must dd if=/dev/urandom of=/$POOL/f1 bs=128k count=4
log_must zfs snapshot $POOL@snap
log_must zfs clone $POOL@snap $POOL/clone
log_must dd if=/dev/urandom of=/$POOL/clone/f1 bs=128k count=1 conv=notrunc
log_must zfs snapshot $POOL/clone@snap
log_must zfs redact $POOL@snap book9 $POOL/clone@snap
log_must eval "zfs send --redact book9 $POOL@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $POOL $recvfs "f1" "$RANGE1"
log_must zfs destroy -R $POOL@snap

log_pass "Redaction works as expected for various scenarios."

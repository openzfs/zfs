#!/bin/ksh

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
# Verify that incrementals (redacted and normal) work with redacted datasets.
#
# Strategy:
# 1. Test normal incrementals from the original snap to a subset of the
#    redaction list.
# 2. Test receipt of intermediate clones, and their children.
# 3. Test receipt with origin snap specified by '-o origin='.
# 4. Test incrementals from redaction bookmarks.
#

typeset ds_name="incrementals"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name '' setup_incrementals
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"

log_onexit redacted_cleanup $sendfs $recvfs $POOL2/rfs

# Setup a redacted send using a redaction list at varying depth.
log_must zfs redact $sendfs@snap0 book1 $POOL/rm@snap $POOL/stride3@snap \
     $POOL/stride5@snap
log_must eval "zfs send --redact book1 $sendfs@snap0 >$stream"
log_must eval "zfs receive $POOL2/rfs <$stream"

# Verify receipt of normal incrementals to redaction list members.
log_must eval "zfs send -i $sendfs@snap0 $POOL/stride3@snap >$stream"
log_must eval "zfs recv $POOL2/rstride3 <$stream"
log_must directory_diff /$POOL/stride3 /$POOL2/rstride3
log_must eval "zfs send -i $sendfs@snap0 $POOL/stride5@snap >$stream"
log_must eval "zfs recv $POOL2/rstride5 <$stream"
log_must directory_diff /$POOL/stride5 /$POOL2/rstride5

# But not a normal child that we weren't redacted with respect to.
log_must eval "zfs send -i $sendfs@snap0 $POOL/hole@snap >$stream"
log_mustnot eval "zfs recv $POOL2/rhole@snap <$stream"

# Verify we can receive an intermediate clone redacted with respect to a
# subset of the original redaction list.
log_must zfs redact $POOL/int@snap book2 $POOL/rm@snap
log_must eval "zfs send -i $sendfs@snap0 --redact book2 $POOL/int@snap >$stream"
log_must eval "zfs recv $POOL2/rint <$stream"
compare_files $POOL/int $POOL2/rint "f1" "$RANGE0"
compare_files $POOL/int $POOL2/rint "f2" "$RANGE15"
compare_files $POOL/int $POOL2/rint "d1/f1" "$RANGE16"
log_must mount_redacted -f $POOL2/rint

# Verify we can receive grandchildren on the child.
log_must eval "zfs send -i $POOL/int@snap $POOL/rm@snap >$stream"
log_must eval "zfs receive $POOL2/rrm <$stream"
log_must directory_diff /$POOL/rm /$POOL2/rrm

# But not a grandchild that the received child wasn't redacted with respect to.
log_must eval "zfs send -i $POOL/int@snap $POOL/write@snap >$stream"
log_mustnot eval "zfs recv $POOL2/rwrite<$stream"

# Verify we cannot receive an intermediate clone that isn't redacted with
# respect to a subset of the original redaction list.
log_must zfs redact $POOL/int@snap book4 $POOL/rm@snap $POOL/write@snap
log_must eval "zfs send -i $sendfs@snap0 --redact book4 $POOL/int@snap >$stream"
log_mustnot eval "zfs recv $POOL2/rint <$stream"
log_must zfs redact $POOL/int@snap book5 $POOL/write@snap
log_must eval "zfs send -i $sendfs@snap0 --redact book5 $POOL/int@snap >$stream"
log_mustnot eval "zfs recv $POOL2/rint <$stream"
log_mustnot zfs redact $POOL/int@snap book6 $POOL/hole@snap

# Verify we can receive a full clone of the grandchild on the child.
log_must eval "zfs send $POOL/write@snap >$stream"
log_must eval "zfs recv -o origin=$POOL2/rint@snap $POOL2/rwrite <$stream"
log_must directory_diff /$POOL/write /$POOL2/rwrite

# Along with other origins.
log_must eval "zfs recv -o origin=$POOL2/rfs@snap0 $POOL2/rwrite1 <$stream"
log_must directory_diff /$POOL/write /$POOL2/rwrite1
log_must eval "zfs recv -o origin=$POOL2@init $POOL2/rwrite2 <$stream"
log_must directory_diff /$POOL/write /$POOL2/rwrite2
log_must zfs destroy -R $POOL2/rwrite2

log_must zfs destroy -R $POOL2/rfs

# Write some data for tests of incremental sends from bookmarks
log_must zfs snapshot $sendfs@snap1
log_must zfs clone $sendfs@snap1 $POOL/hole1
typeset mntpnt=$(get_prop mountpoint $POOL/hole1)
log_must dd if=/dev/zero of=$mntpnt/f2 bs=128k count=16 conv=notrunc
log_must zfs snapshot $POOL/hole1@snap
log_must zfs clone $sendfs@snap1 $POOL/write1
mntpnt=$(get_prop mountpoint $POOL/write1)
log_must dd if=/dev/urandom of=$mntpnt/f2 bs=128k count=16 conv=notrunc
log_must zfs snapshot $POOL/write1@snap
log_must zfs clone $POOL/int@snap $POOL/write2
mntpnt=$(get_prop mountpoint $POOL/write2)
log_must dd if=/dev/urandom of=$mntpnt/f2 bs=128k count=16 conv=notrunc
log_must zfs snapshot $POOL/write2@snap

# Setup a redacted send using a redaction list at varying depth.
log_must zfs redact $sendfs@snap0 book7 $POOL/rm@snap $POOL/stride3@snap \
     $POOL/stride5@snap
log_must eval "zfs send --redact book7 $sendfs@snap0 >$stream"
log_must eval "zfs receive $POOL2/rfs <$stream"

# Verify we can receive a redacted incremental sending from the bookmark.
log_must zfs redact $sendfs@snap1 book8 $POOL/write1@snap
log_must eval "zfs send -i $sendfs#book7 --redact book8 $sendfs@snap1 >$stream"
log_must eval "zfs receive $POOL2/rfs <$stream"
# The stride3 and stride5 snaps redact 3 128k blocks at block offsets 0 15 and
# 30 of f2. The write1 snap only covers the first two of those three blocks.
compare_files $sendfs $POOL2/rfs "f2" "$RANGE12"
log_must mount_redacted -f $POOL2/rfs
log_must diff $send_mnt/f1 /$POOL2/rfs/f1
log_must diff $send_mnt/d1/f1 /$POOL2/rfs/d1/f1
unmount_redacted $POOL2/rfs

# Verify we can receive a normal child we weren't redacted with respect to by
# sending from the bookmark.
log_must eval "zfs send -i $sendfs#book7 $POOL/hole1@snap >$stream"
log_must eval "zfs recv $POOL2/rhole1 <$stream"
log_must directory_diff /$POOL/hole1 /$POOL2/rhole1

# Verify we can receive an intermediate clone redacted with respect to a
# non-subset if we send from the bookmark.
log_must zfs redact $POOL/int@snap book9 $POOL/write2@snap
log_must eval "zfs send -i $sendfs#book7 --redact book9 $POOL/int@snap >$stream"
log_must eval "zfs receive $POOL2/rint <$stream"
compare_files $sendfs $POOL2/rint "f2" "$RANGE12"

log_pass "Incrementals (redacted and normal) work with redacted datasets."

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
# Copyright (c) 2017, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify redaction works as expected with respect to deleted files
#
# Strategy:
# 1. A file on the delete queue counts as deleted when using it to calculate
#    redaction.
# 2. A file that is removed in the tosnap of an incremental, where the fromsnap
#    is a redaction bookmark that contains references to that file, does not
#    result in records for that file.
#

typeset ds_name="deleted"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset clone2="$POOL/${ds_name}_clone2"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name ''
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"

log_onexit redacted_cleanup $sendfs $recvfs

#
# A file on the delete queue counts as deleted when using it to calculate
# redaction.
#

#
# Open file descriptor 5 for appending to $clone_mnt/f1 so that it will go on
# the delete queue when we rm it.
#
exec 5>>$clone_mnt/f1
log_must dd if=/dev/urandom of=$clone_mnt/f1 bs=512 count=1 conv=notrunc
log_must rm $clone_mnt/f1
log_must zfs snapshot $clone@snap1
# Close file descriptor 5
exec 5>&-
log_must zfs redact $sendfs@snap book1 $clone@snap1
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must mount_redacted -f $recvfs
#
# We have temporarily disabled redaction blkptrs, so this will not
# fail as was originally intended.  We should uncomment this line
# when we re-enable redaction blkptrs.
#
#log_mustnot dd if=$recv_mnt/f1 of=/dev/null bs=512 count=1
log_must diff $send_mnt/f2 $recv_mnt/f2
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

#
# A file that is removed in the tosnap of an incremental, where the fromsnap
# is a redaction bookmark that contains references to that file, does not
# result in records for that file.
#
log_must zfs clone  $sendfs@snap $clone2
typeset clone2_mnt="$(get_prop mountpoint $clone2)"
log_must rm -rf $clone2_mnt/*
log_must zfs snapshot $clone2@snap
log_must zfs redact $sendfs@snap book2 $clone2@snap
log_must zfs destroy -R $clone2
log_must eval "zfs send --redact book2 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must rm $send_mnt/f1
log_must zfs snapshot $sendfs@snap2
log_must zfs clone  $sendfs@snap2 $clone2
typeset clone2_mnt="$(get_prop mountpoint $clone2)"
log_must rm $clone2_mnt/*
log_must zfs snapshot $clone2@snap
log_must zfs redact $sendfs@snap2 book3 $clone2@snap
log_must zfs destroy -R $clone2
log_must eval "zfs send -i $sendfs#book2 --redact book3 $sendfs@snap2 >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must mount_redacted -f $recvfs
log_must diff <(ls $send_mnt) <(ls $recv_mnt)
log_must zfs destroy -R $recvfs
log_must zfs rollback -R $sendfs@snap

log_pass "Verify Redaction works as expected with respect to deleted files."

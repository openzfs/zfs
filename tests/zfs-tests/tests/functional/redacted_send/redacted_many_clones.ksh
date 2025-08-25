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
# Verify redacted send can deal with a large redaction list.
#
# Strategy:
# 1. Create 64 clones of sendfs each of which modifies two blocks in a file.
#    The first modification is at an offset unique to each clone, and the
#    second (the last block in the file) is common to them all.
# 2. Verify a redacted stream with a reasonable redaction list length can
#    be correctly processed.
#

typeset ds_name="many_clones"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name ''
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"
typeset redaction_list=''
typeset mntpnt

log_onexit redacted_cleanup $sendfs $recvfs

# Fill in both the last block, and a different block in every clone.
for i in {1..64}; do
	log_must zfs clone $sendfs@snap ${clone}$i
	mntpnt=$(get_prop mountpoint ${clone}$i)
	log_must dd if=/dev/urandom of=$mntpnt/f2 bs=64k count=1 seek=$i \
	    conv=notrunc
	log_must dd if=/dev/urandom of=$mntpnt/f2 bs=64k count=1 seek=63 \
	    conv=notrunc
	log_must zfs snapshot ${clone}$i@snap
done

# The limit isn't necessarily 64 snapshots. The maximum number of snapshots in
# the redacted list is determined in dsl_bookmark_create_redacted_check().
log_must zfs redact $sendfs@snap book1 $clone{1..64}@snap
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f2" "$RANGE8"

rls_value="$(zpool get -H -o value feature@redaction_list_spill $POOL)"
if [ "$rls_value" = "active" ]; then
	log_note "redaction_list_spill feature active"
else
	log_fail "redaction_list_spill feature not active"
fi

log_pass "Redacted send can deal with a large redaction list."

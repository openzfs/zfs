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
# Test that redacted send correctly detects invalid arguments.
#

typeset sendfs="$POOL2/sendfs"
typeset recvfs="$POOL2/recvfs"
typeset clone1="$POOL2/clone1"
typeset clone2="$POOL2/clone2"
typeset clone3="$POOL2/clone3"
typeset clone3="$POOL2/clone4"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)

log_onexit redacted_cleanup $sendfs $recvfs $clone3

log_must zfs create $sendfs
log_must zfs snapshot $sendfs@snap1
log_must zfs snapshot $sendfs@snap2
log_must zfs snapshot $sendfs@snap3
log_must zfs clone $sendfs@snap2 $clone1
log_must zfs snapshot $clone1@snap
log_must zfs bookmark $clone1@snap $clone1#book
log_must zfs clone $sendfs@snap2 $clone2
log_must zfs snapshot $clone2@snap

# Incompatible flags
log_must zfs redact $sendfs@snap2 book $clone1@snap
log_mustnot eval "zfs send -R --redact book $sendfs@snap2 > /dev/null"

typeset arg
for arg in "$sendfs" "$clone1#book"; do
	log_mustnot eval "zfs send --redact book $arg > /dev/null"
done

# Bad redaction list arguments
log_mustnot zfs redact $sendfs@snap1
log_mustnot zfs redact $sendfs@snap1 book
log_mustnot zfs redact $sendfs#book1 book4 $clone1
log_mustnot zfs redact $sendfs@snap1 book snap2 snap3
log_mustnot zfs redact $sendfs@snap1 book @snap2 @snap3
log_mustnot eval "zfs send --redact $sendfs#book $sendfs@snap > /dev/null"

# Redaction snapshots not a descendant of tosnap
log_mustnot zfs redact $sendfs@snap2 book $sendfs@snap2
log_must zfs redact $sendfs@snap2 book2 $clone1@snap $clone2@snap
log_must eval "zfs send --redact book2 $sendfs@snap2 >$stream"
log_must zfs redact $sendfs@snap2 book3 $clone1@snap $clone2@snap
log_must eval "zfs send -i $sendfs@snap1 --redact book3 $sendfs@snap2 \
    > /dev/null"
log_mustnot zfs redact $sendfs@snap3 $sendfs@snap3 $clone1@snap

# Full redacted sends of redacted datasets are not allowed.
log_must eval "zfs recv $recvfs <$stream"
log_must zfs snapshot $recvfs@snap
log_must zfs clone $recvfs@snap $clone3
log_must zfs snapshot $clone3@snap
log_mustnot zfs redact $recvfs@snap book5 $clone3@snap

# Nor may a redacted dataset appear in the redaction list.
log_mustnot zfs redact testpool2/recvfs@snap2 book7 testpool2/recvfs@snap

# Non-redaction bookmark cannot be sent and produces invalid argument error
log_must zfs bookmark "$sendfs@snap1" "$sendfs#book8"
log_must eval "zfs send --redact book8 -i $sendfs@snap1 $sendfs@snap2 2>&1 | head -n 100 | grep 'not a redaction bookmark'"

# Error messages for common usage errors
log_mustnot_expect "not contain '#'"    zfs redact $sendfs@snap1 \#book $sendfs@snap2
log_mustnot_expect "not contain '#'"    zfs redact $sendfs@snap1 $sendfs#book $sendfs@snap2
log_mustnot_expect "full dataset names" zfs redact $sendfs@snap1 book @snap2
log_mustnot_expect "full dataset names" zfs redact $sendfs@snap1 book @snap2
log_mustnot_expect "full dataset names" zfs redact $sendfs@snap1 \#book @snap2
log_mustnot_expect "descendent of snapshot" zfs redact $sendfs@snap2 book $sendfs@snap1

log_pass "Verify that redacted send correctly detects invalid arguments."

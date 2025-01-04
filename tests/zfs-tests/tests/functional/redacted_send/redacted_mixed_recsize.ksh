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
# Verify redacted send works with datasets of different sizes.
#
# Strategy:
# 1. Create two dataset one with recsize 512, and one 1m and create a 2m file.
# 2. For each dataset, create clones of both 512 and 1m recsize and modify
#    the first 16k of the file.
# 3. Send each original dataset, redacted with respect to each of the clones
#    into both a dataset inheriting a 512 recsize and a 1m one.
# 4. Verify that the smallest unit of redaction is that of the origin fs.
#

typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
typeset mntpnt

log_onexit redacted_cleanup $POOL/512 $POOL/1m $POOL2/512 $POOL2/1m

# Set up the datasets we'll send and redact from.
log_must zfs create -o recsize=512 $POOL/512
mntpnt=$(get_prop mountpoint $POOL/512)
log_must dd if=/dev/urandom of=$mntpnt/f1 bs=1024k count=2
log_must zfs snapshot $POOL/512@snap
log_must zfs clone -o recsize=1m $POOL/512@snap $POOL/1mclone
mntpnt=$(get_prop mountpoint $POOL/1mclone)
log_must dd if=/dev/urandom of=$mntpnt/f1 bs=512 count=32 conv=notrunc
log_must zfs snapshot $POOL/1mclone@snap

log_must zfs create -o recsize=1m $POOL/1m
mntpnt=$(get_prop mountpoint $POOL/1m)
log_must dd if=/dev/urandom of=$mntpnt/f1 bs=1024k count=2
log_must zfs snapshot $POOL/1m@snap
log_must zfs clone -o recsize=512 $POOL/1m@snap $POOL/512clone
mntpnt=$(get_prop mountpoint $POOL/512clone)
log_must dd if=/dev/urandom of=$mntpnt/f1 bs=512 count=32 conv=notrunc
log_must zfs snapshot $POOL/512clone@snap

# Create datasets that allow received datasets to inherit recordsize.
log_must zfs create -o recsize=512 $POOL2/512
log_must zfs create -o recsize=1m $POOL2/1m

# Do the sends and verify the contents.
log_must zfs redact $POOL/512@snap book1 $POOL/1mclone@snap
log_must eval "zfs send --redact book1 $POOL/512@snap>$stream"
log_must eval "zfs recv $POOL2/512/recva <$stream"
compare_files $POOL/512 $POOL2/512/recva "f1" "$RANGE13"
log_must eval "zfs recv $POOL2/1m/recvb <$stream"
compare_files $POOL/512 $POOL2/1m/recvb "f1" "$RANGE13"

log_must zfs redact $POOL/1m@snap book2 $POOL/512clone@snap
log_must eval "zfs send --redact book2 $POOL/1m@snap >$stream"
log_must eval "zfs recv $POOL2/512/recvc <$stream"
compare_files $POOL/1m $POOL2/512/recvc "f1" "$RANGE11"
log_must eval "zfs recv $POOL2/1m/recvd <$stream"
compare_files $POOL/1m $POOL2/1m/recvd "f1" "$RANGE11"

log_pass "Redaction works correctly with different recordsizes."

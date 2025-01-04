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
# Verify that send size estimates of redacted sends work correctly
#
# Strategy:
# 1. Perform a redacted send with -nv and without, and verify the
#    size estimate is the same as the size of the actual send.
# 2. Receive an incremental send from the redaction bookmark with
#    -nv and without, and verify the size estimate is the same as
#    the size of the actual send.
#

ds_name="sizes"
typeset sendfs="$POOL/$ds_name"
typeset clone="$POOL/${ds_name}_clone2"
setup_dataset $ds_name "-o compress=lz4"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset size=$(mktemp $tmpdir/size.XXXX)
typeset size2=$(mktemp $tmpdir/size.XXXX)

log_onexit redacted_cleanup $sendfs $clone
log_must zfs clone $sendfs@snap $clone
typeset clone_mnt="$(get_prop mountpoint $clone)"
log_must rm -rf $clone_mnt/*
log_must zfs snapshot $clone@snap
log_must zfs redact $sendfs@snap book $clone@snap
log_must eval "zfs send -nvP --redact book $sendfs@snap | awk '/^size/ {print \$2}' >$size"
log_must eval "zfs send --redact book $sendfs@snap | wc -c >$size2"
read -r bytes1 < $size
read -r bytes2 < $size2
[ "$bytes1" -eq "$bytes2" ] || \
    log_fail "Full sizes differ: estimate $bytes1 and actual $bytes2"

log_must zfs snapshot $sendfs@snap2
log_must eval "zfs send -nvP -i $sendfs#book $sendfs@snap2 | awk '/^size/ {print \$2}' >$size"
log_must eval "zfs send -i $sendfs#book $sendfs@snap2 | wc -c >$size2"
read -r bytes1 < $size
read -r bytes2 < $size2
[ "$bytes1" -eq "$bytes2" ] || \
    log_fail "Incremental sizes differ: estimate $bytes1 and actual $bytes2"

log_pass "Size estimates of redacted sends estimate accurately."

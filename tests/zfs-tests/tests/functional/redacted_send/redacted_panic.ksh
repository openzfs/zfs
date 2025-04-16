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
# Copyright (c) 2021 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify edge case when midbufid is equal to minbufid for the bug fixed by
# https://github.com/openzfs/zfs/pull/11297 (Fix kernel panic induced by
# redacted send)
#

typeset ds_name="panic"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset stream=$(mktemp -t stream.XXXX)

function cleanup
{
	redacted_cleanup $sendfs $recvfs
	rm -f $stream
}

log_onexit cleanup

log_must zfs create -o recsize=8k $sendfs
log_must dd if=/dev/urandom of=/$sendfs/file bs=1024k count=1024
log_must zfs snapshot $sendfs@init
log_must zfs clone $sendfs@init $clone
log_must stride_dd -i /dev/urandom -o /$clone/file -b 8192 -s 2 -c 7226
log_must zfs snapshot $clone@init
log_must zfs redact $sendfs@init book_init $clone@init
log_must eval "zfs send --redact $sendfs#book_init $sendfs@init >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_pass

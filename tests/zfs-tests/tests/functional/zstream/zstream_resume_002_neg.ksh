#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that "zstream resume" rejects streams that do not match the
# resume token.
#
# Strategy:
# 1. Interrupt a receive of a compressed stream to get a resume token.
# 2. Verify that zstream resume rejects a malformed token, a stream of a
#    different snapshot, a stream sent with different options, a
#    replication stream, and a stream that ends before the resume point.
# 3. Verify that the matching stream is accepted.
#

verify_runnable "both"

log_assert "Verify zstream resume rejects streams that do not match."
log_onexit cleanup_pool $POOL

typeset sendfs=$POOL/fs
typeset recvfs=$POOL/recv
typeset stream=$BACKDIR/stream
typeset other=$BACKDIR/other
typeset partial=$BACKDIR/partial

log_must zfs create -o compress=lz4 $sendfs
typeset dir=$(get_prop mountpoint $sendfs)
typeset -i i
for ((i = 0; i < 8; i++)); do
	log_must dd if=/dev/urandom of=$dir/file$i bs=128k count=8 status=none
done
log_must zfs snapshot $sendfs@a
log_must dd if=/dev/urandom of=$dir/extra bs=128k count=8 status=none
log_must zfs snapshot $sendfs@b

log_must eval "zfs send -c $sendfs@a > $stream"
typeset -i size=$(stat_size $stream)
log_must eval "head -c $((size / 2)) $stream > $partial"
log_mustnot eval "zfs recv -s $recvfs < $partial"
typeset token=$(get_prop receive_resume_token $recvfs)
log_must test "$token" != "-"

log_mustnot eval "zstream resume 1-bogus-token $stream > /dev/null"
log_mustnot eval "zstream resume > /dev/null"

log_must eval "zfs send -c $sendfs@b > $other"
log_mustnot eval "zstream resume $token $other > /dev/null"

log_must eval "zfs send $sendfs@a > $other"
log_mustnot eval "zstream resume $token $other > /dev/null"

log_must eval "zfs send -c -R $sendfs@a > $other"
log_mustnot eval "zstream resume $token $other > /dev/null"

log_must eval "head -c $((size / 8)) $stream > $other"
log_mustnot eval "zstream resume $token $other > /dev/null"

# Records before the resume point may be skipped without being read, but
# everything after it must still be validated.
log_must cp $stream $other
log_must dd if=/dev/urandom of=$other bs=1 count=8 seek=$((size * 3 / 4)) \
    conv=notrunc status=none
log_mustnot eval "zstream resume $token $other > /dev/null"
log_mustnot eval "cat $other | zstream resume $token > /dev/null"

log_must eval "zstream resume $token $stream > /dev/null"

log_pass "zstream resume rejects streams that do not match."

#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version a.0.
# You may only use this file in accordance with the terms of version
# a.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2019 Datto Inc.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that a partially received dataset can be sent with
# 'zfs send --saved'.
#
# Strategy:
# 1. Setup a pool with partially received filesystem
# 2. Perform saved send without incremental
# 3. Perform saved send with incremental
# 4. Perform saved send with incremental, resuming from a token
# 5. Perform negative tests for invalid command inputs
#

verify_runnable "both"

log_assert "Verify that a partially received dataset can be sent with " \
	"'zfs send --saved'."

function cleanup
{
	destroy_dataset $POOL/testfs2 "-r"
	destroy_dataset $POOL/stream "-r"
	destroy_dataset $POOL/recvfs "-r"
	destroy_dataset $POOL/partialfs "-r"
}
log_onexit cleanup

log_must zfs create $POOL/testfs2
log_must zfs create $POOL/stream
mntpnt=$(get_prop mountpoint $POOL/testfs2)

# Setup a pool with partially received filesystems
log_must mkfile 1m $mntpnt/filea
log_must zfs snap $POOL/testfs2@a
log_must mkfile 1m $mntpnt/fileb
log_must zfs snap $POOL/testfs2@b
log_must eval "zfs send $POOL/testfs2@a | zfs recv $POOL/recvfs"
log_must eval "zfs send -i $POOL/testfs2@a $POOL/testfs2@b > " \
	"/$POOL/stream/inc.send"
log_must eval "zfs send $POOL/testfs2@b > /$POOL/stream/full.send"
mess_send_file /$POOL/stream/full.send
mess_send_file /$POOL/stream/inc.send
log_mustnot zfs recv -s $POOL/recvfullfs < /$POOL/stream/full.send
log_mustnot zfs recv -s $POOL/recvfs < /$POOL/stream/inc.send

# Perform saved send without incremental
log_mustnot eval "zfs send --saved $POOL/recvfullfs | zfs recv -s " \
	"$POOL/partialfs"
token=$(zfs get -Hp -o value receive_resume_token $POOL/partialfs)
log_must eval "zfs send -t $token | zfs recv -s $POOL/partialfs"
file_check $POOL/recvfullfs $POOL/partialfs
log_must zfs destroy -r $POOL/partialfs

# Perform saved send with incremental
log_must eval "zfs send $POOL/recvfs@a | zfs recv $POOL/partialfs"
log_mustnot eval "zfs send --saved $POOL/recvfs | " \
	"zfs recv -s $POOL/partialfs"
token=$(zfs get -Hp -o value receive_resume_token $POOL/partialfs)
log_must eval "zfs send -t $token | zfs recv -s $POOL/partialfs"
file_check $POOL/recvfs $POOL/partialfs
log_must zfs destroy -r $POOL/partialfs

# Perform saved send with incremental, resuming from token
log_must eval "zfs send $POOL/recvfs@a | zfs recv $POOL/partialfs"
log_must eval "zfs send --saved $POOL/recvfs > " \
	"/$POOL/stream/partial.send"
mess_send_file /$POOL/stream/partial.send
log_mustnot zfs recv -s $POOL/partialfs < /$POOL/stream/partial.send
token=$(zfs get -Hp -o value receive_resume_token $POOL/partialfs)
log_must eval "zfs send -t $token | zfs recv -s $POOL/partialfs"
file_check $POOL/recvfs $POOL/partialfs

# Perform negative tests for invalid command inputs
set -A badargs \
	"" \
	"$POOL/recvfs@a" \
	"-i $POOL/recvfs@a $POOL/recvfs@b" \
	"-R $POOL/recvfs" \
	"-p $POOL/recvfs" \
	"-I $POOL/recvfs" \
	"-h $POOL/recvfs"

while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "zfs send --saved ${badargs[i]} >/dev/null"
	(( i = i + 1 ))
done

log_pass "A partially received dataset can be sent with 'zfs send --saved'."

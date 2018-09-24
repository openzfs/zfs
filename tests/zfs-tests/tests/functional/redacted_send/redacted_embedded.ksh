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
# Verify embedded blocks and redacted send work correctly together.
#
# Strategy:
# 1. Create recsize sized files with embedded blocks from size 512b to 16k.
# 2. Receive a redacted send stream with nothing redacted.
# 3. Verify the received files match the source, contain embedded blocks, and
#    that the stream has the redacted and embedded data features.
# 4. Receive a redacted send stream with files 512, 2048 and 8192 redacted.
# 5. Verify that the redacted files no longer match, but the others still
#    contain embedded blocks and the stream has the redacted and embedded
#    data features.
#

typeset ds_name="embedded"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name '-o compress=lz4' setup_embedded
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"
typeset recsize send_obj recv_obj

log_onexit redacted_cleanup $sendfs $recvfs

log_must zfs redact $sendfs@snap book1 $clone@snap
log_must eval "zfs send -e --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must stream_has_features $stream redacted embed_data

log_must mount_redacted -f $recvfs
for recsize in 512 1024 2048 4096 8192 16384; do
	send_obj=$(get_objnum $send_mnt/$recsize)
	recv_obj=$(get_objnum $recv_mnt/$recsize)

	log_must diff $send_mnt/$recsize $recv_mnt/$recsize
	log_must eval "zdb -ddddd $sendfs $send_obj >$tmpdir/send.zdb"
	log_must eval "zdb -ddddd $recvfs $recv_obj >$tmpdir/recv.zdb"

	grep -q "EMBEDDED" $tmpdir/send.zdb || \
	    log_fail "Obj $send_obj not embedded in $sendfs"
	grep -q "EMBEDDED" $tmpdir/recv.zdb || \
	    log_fail "Obj $recv_obj not embedded in $recvfs"

	cat $stream | zstreamdump -v | log_must grep -q \
	    "WRITE_EMBEDDED object = $send_obj offset = 0"
done

log_must zfs destroy -R $recvfs
for recsize in 512 2048 8192; do
	log_must dd if=/dev/urandom of=$clone_mnt/$recsize bs=$recsize count=1
done
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book2 $clone@snap1
log_must eval "zfs send -e --redact book2 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must stream_has_features $stream redacted embed_data

log_must mount_redacted -f $recvfs
for recsize in 512 2048 8192; do
	log_mustnot diff $send_mnt/$recsize $recv_mnt/$recsize
done
for recsize in 1024 4096 16384; do
	send_obj=$(get_objnum $send_mnt/$recsize)
	recv_obj=$(get_objnum $recv_mnt/$recsize)

	log_must diff $send_mnt/$recsize $recv_mnt/$recsize
	log_must eval "zdb -ddddd $sendfs $send_obj >$tmpdir/send.zdb"
	log_must eval "zdb -ddddd $recvfs $recv_obj >$tmpdir/recv.zdb"

	grep -q "EMBEDDED" $tmpdir/send.zdb || \
	    log_fail "Obj $send_obj not embedded in $sendfs"
	grep -q "EMBEDDED" $tmpdir/recv.zdb || \
	    log_fail "Obj $recv_obj not embedded in $recvfs"

	cat $stream | zstreamdump -v | log_must grep -q \
	    "WRITE_EMBEDDED object = $send_obj offset = 0"
done

log_pass "Embedded blocks and redacted send work correctly together."

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

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that zstream decompress leaves WRITE records that are not
# compressed in the stream unchanged, both when no compression type is
# given and when the type is "off".
#
# Strategy:
# 1. Create a dataset with compression=off and write a file spanning
#    two full records, then snapshot and send it
# 2. Run zstream decompress on both records, once without a compression
#    type and once with "off"
# 3. Verify each output is byte-identical to the input stream and that
#    zstream dump can parse it
# 4. Verify each output can be received and yields an identical file
#

verify_runnable "both"

log_assert "Verify zstream decompress passes uncompressed records through."
log_onexit cleanup_pool $POOL

typeset sendfs=$POOL/fs
typeset stream=$BACKDIR/uncompressed.zsend
typeset out_default=$BACKDIR/uncompressed-default.zsend
typeset out_off=$BACKDIR/uncompressed-off.zsend

log_must zfs create -o compression=off -o recordsize=128k $sendfs
typeset dir=$(get_prop mountpoint $sendfs)
log_must dd if=/dev/urandom of=$dir/file bs=128k count=2
log_must zfs snapshot $sendfs@snap
typeset obj=$(get_objnum $dir/file)

log_must eval "zfs send $sendfs@snap > $stream"

log_must eval "zstream decompress $obj,0 $obj,131072 < $stream > $out_default"
log_must eval "zstream decompress $obj,0,off $obj,131072,off " \
    "< $stream > $out_off"

typeset i=0
for out in $out_default $out_off; do
	log_must cmp $stream $out
	log_must eval "zstream dump -v < $out > $out.dump"
	log_must eval "zfs recv $POOL/recv$i < $out"
	log_must cmp $dir/file $(get_prop mountpoint $POOL/recv$i)/file
	((i++))
done

log_pass "zstream decompress passes uncompressed records through."

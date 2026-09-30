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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that zfs recv and zstream reject uncompressed (ZIO_COMPRESS_OFF)
# DRR_WRITE_EMBEDDED records.
#
# Why this test exists:
# receive_write_embedded() has no ZIO_COMPRESS_OFF path: when it has to
# split an embedded record across the object's blocks it always
# decompresses the payload with zio_decompress_data().  An uncompressed
# path was removed as dead code, because no sender produces uncompressed
# embedded records.  zio_write_compress() embeds a block in its block
# pointer only after compression has shrunk it to at most
# BPE_PAYLOAD_SIZE (112) bytes, so an embedded data block is never
# ZIO_COMPRESS_OFF.  An uncompressed payload would have to be the whole
# block, and no block is smaller than SPA_MINBLOCKSIZE (512), so
# recv_check_drr_write_embedded() rejects every such record by its
# minimum size checks.
#
# If this test starts failing because such records are accepted,
# receive_write_embedded() must regain ZIO_COMPRESS_OFF handling before
# the validator is relaxed.
#
# Without the minimum size checks these records were received, leaving
# an embedded block pointer with a 64 or 112 byte logical size in an
# object with 4096-byte blocks.
#
# Strategy:
# 1. For each prebuilt stream, whose first DRR_WRITE_EMBEDDED record (a
#    4096-byte file in an object with 4096-byte blocks) was rewritten to
#    ZIO_COMPRESS_OFF with a raw payload:
#    - embedded-uncompressed-64: lsize = psize = length = 64
#    - embedded-uncompressed-112: lsize = psize = length = 112
#    - embedded-uncompressed-112-len4096: lsize = psize = 112,
#      length = 4096 (the object's block size)
#    a. Confirm zstream dump rejects it with the expected message.
#    b. Confirm zfs recv fails with the same message.
#    c. Confirm no dataset is left behind.
# 2. Confirm a genuine compressed embedded block can still be received
#    under the same name and reads back intact.
#

verify_runnable "both"

typeset dir=$STF_SUITE/tests/functional/rsend
typeset bad=$BACKDIR/embedded-uncompressed.zsend
typeset err=$BACKDIR/embedded-uncompressed.err
typeset good=$BACKDIR/embedded-compressed.zsend
typeset badrecv=$POOL/badrecv
typeset embsrc=$POOL/embsrc

function cleanup
{
	datasetexists $badrecv && destroy_dataset $badrecv -r
	datasetexists $embsrc && destroy_dataset $embsrc -r
	rm -f $bad $err $good
}
log_onexit cleanup

#
# check_rejected NAME MESSAGE
#
function check_rejected
{
	typeset name=$1
	typeset msg=$2

	log_must eval "bzcat <$dir/$name.zsend.bz2 >$bad"

	log_mustnot eval "zstream dump $bad >$err 2>&1"
	log_must grep -q "$msg" $err
	log_note "$name: zstream dump rejected: $(grep DRR_ $err | head -1)"

	datasetexists $badrecv && log_must zfs destroy -r $badrecv
	log_mustnot eval "zfs recv $badrecv <$bad >$err 2>&1"
	log_must grep -q "$msg" $err
	log_note "$name: zfs recv rejected: $(head -1 $err)"
	log_mustnot datasetexists $badrecv
}

log_assert "uncompressed DRR_WRITE_EMBEDDED records are rejected"

log_must mkdir -p $BACKDIR

check_rejected embedded-uncompressed-64 \
    "DRR_WRITE_EMBEDDED length 64 below minimum 512"
check_rejected embedded-uncompressed-112 \
    "DRR_WRITE_EMBEDDED length 112 below minimum 512"
check_rejected embedded-uncompressed-112-len4096 \
    "DRR_WRITE_EMBEDDED logical size 112 below minimum 512"

# A genuine (compressed) embedded block still receives under that name.
log_must zfs create -o compression=lz4 -o recordsize=4k $embsrc
typeset mntpnt=$(get_prop mountpoint $embsrc)
log_must eval "printf '%.0sembedded data..\\n' {1..256} >$mntpnt/file"
log_must zfs snapshot $embsrc@s
log_must eval "zfs send -e $embsrc@s >$good"
log_must eval "zstream dump -v $good | \\
    grep -q 'WRITE_EMBEDDED.* length = 4096 .* lsize = 4096 '"
log_must eval "zfs recv $badrecv <$good"
log_must cmp $mntpnt/file $(get_prop mountpoint $badrecv)/file

log_pass "uncompressed DRR_WRITE_EMBEDDED records are rejected"

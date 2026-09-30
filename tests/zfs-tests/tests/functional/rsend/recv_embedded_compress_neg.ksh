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
# Verify that zfs recv and zstream reject DRR_WRITE_EMBEDDED records whose
# compression type is not a compression algorithm.
#
# Why this test exists:
# An embedded block pointer holds a compressed payload, and only an
# algorithm with a decompressor can be written into one or read back.
# zio_write_compress() embeds a block only after compressing it into the
# block pointer, so a sender never produces an embedded record with
# ZIO_COMPRESS_INHERIT (0) or ZIO_COMPRESS_ON (1), which are property
# values rather than algorithms, or with ZIO_COMPRESS_OFF (2) or
# ZIO_COMPRESS_EMPTY (4), which have no decompressor.
# recv_check_drr_write_embedded() once checked only that the type was
# below ZIO_COMPRESS_FUNCTIONS, so a record with INHERIT or ON was
# passed to dmu_write_embedded(), where encode_embedded_bp_compressed()
# asserts comp >= ZIO_COMPRESS_OFF (a panic on debug builds), and one
# with EMPTY stored a block pointer that decode_embedded_bp() cannot
# decompress when the block is read.  The validator must keep rejecting
# every type without a decompressor.
#
# Strategy:
# 1. For each prebuilt stream, made from a genuine zfs send -e stream of a
#    4096-byte lz4 file in an object with 4096-byte blocks (lsize =
#    length = 4096, psize 57) by changing only the first
#    DRR_WRITE_EMBEDDED record's compression type, to INHERIT (0),
#    ON (1), OFF (2) or EMPTY (4):
#    a. Confirm zstream dump rejects it with the expected message.
#    b. Confirm zfs recv fails with the same message.
#    c. Confirm no dataset is left behind.
# 2. Confirm a genuine compressed embedded block can still be received
#    under the same name and reads back intact.
#

verify_runnable "both"

typeset dir=$STF_SUITE/tests/functional/rsend
typeset bad=$BACKDIR/embedded-compress.zsend
typeset err=$BACKDIR/embedded-compress.err
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

log_assert "DRR_WRITE_EMBEDDED records without a compression algorithm" \
    "are rejected"

log_must mkdir -p $BACKDIR

typeset msg="DRR_WRITE_EMBEDDED has invalid compression type"
check_rejected embedded-compress-inherit "$msg 0"
check_rejected embedded-compress-on "$msg 1"
check_rejected embedded-compress-off "$msg 2"
check_rejected embedded-compress-empty "$msg 4"

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

log_pass "DRR_WRITE_EMBEDDED records without a compression algorithm" \
    "are rejected"

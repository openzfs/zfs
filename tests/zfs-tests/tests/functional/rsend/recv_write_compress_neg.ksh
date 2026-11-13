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
# Verify that zfs recv and zstream reject compressed DRR_WRITE records and
# raw DRR_WRITE and DRR_SPILL records whose compression type the receiver
# could not read back, and still accept genuine ones.
#
# Why this test exists:
# A compressed (zfs send -c) DRR_WRITE, and a raw (zfs send -w) DRR_WRITE
# or DRR_SPILL, is written to disk as-is, with the record's compression
# type in the new block pointer.  The sender copies that type from
# BP_GET_COMPRESS(), and zio_write_compress() stores only ZIO_COMPRESS_OFF
# or an algorithm with a decompressor there: ZIO_COMPRESS_INHERIT (0) and
# ZIO_COMPRESS_ON (1) are property values, and ZIO_COMPRESS_EMPTY (4)
# becomes OFF or a hole.  An OFF block holds the logical block itself, so
# its payload is the logical size.  A compressed (non-raw) DRR_WRITE is
# never OFF: dump_write() sets the compression fields only for blocks that
# are compressed.  The validators once checked only that the type was
# below ZIO_COMPRESS_FUNCTIONS, so:
#  - a compressed DRR_WRITE with ON or EMPTY was received, and reading the
#    file failed with EIO;
#  - one with OFF (payload smaller than the logical size) was received,
#    and reading the file returned the full logical size, with bytes past
#    the payload that were not the file's data;
#  - a raw DRR_WRITE with ON, OFF or EMPTY, or a raw DRR_SPILL with OFF
#    or EMPTY, was received, and the dataset then failed to mount with
#    EIO;
#  - a raw DRR_SPILL with INHERIT or ON reached arc_alloc_raw_buf(), which
#    asserts compression_type >= ZIO_COMPRESS_OFF (a panic on debug
#    builds).
#
# The prebuilt streams (*.zsend.bz2) are made from genuine streams by
# changing only the compression type byte of one record and recomputing
# the stream checksums; no sender produces them:
#  - write-compress-{on,off,empty}: zfs send -c of a 16384-byte text file
#    in an lz4 dataset (object 2, logical size 16384, compressed size
#    13824, lz4); the file's DRR_WRITE gets type ON (1), OFF (2) or
#    EMPTY (4).
#  - write-raw-compress-{inherit,on,off,empty}: zfs send -w of a
#    16384-byte file of repeated text in an encrypted lz4 dataset with
#    xattr=sa and a 2700-byte xattr; the file's raw DRR_WRITE (logical
#    size 16384, compressed size 512, lz4) gets type INHERIT (0), ON (1),
#    OFF (2) or EMPTY (4).  A raw record of type 0 is read as uncompressed,
#    with logical size payload bytes, so the INHERIT stream's payload is
#    zero-padded to 16384 bytes.
#  - spill-raw-compress-{inherit,on,off,empty}: the same raw stream with
#    the file's DRR_SPILL (length 3072, compressed size 512, lz4) set to
#    type INHERIT (0), ON (1), OFF (2) or EMPTY (4).
#
# Strategy:
# 1. For each prebuilt stream:
#    a. Confirm zstream dump rejects it with the expected message.
#    b. Confirm zfs recv fails with the same message.
#    c. Confirm no dataset is left behind.
# 2. Confirm genuine zfs send -c and zfs send -w streams of the same
#    kind of data, which carry lz4 DRR_WRITE records, raw OFF records
#    whose compressed size equals their logical size, and a compressed raw
#    DRR_SPILL record, are accepted by zstream dump, receive under the
#    same names and read back intact.
#

verify_runnable "both"

typeset dir=$STF_SUITE/tests/functional/rsend
typeset bad=$BACKDIR/write-compress.zsend
typeset err=$BACKDIR/write-compress.err
typeset good=$BACKDIR/write-compress-good.zsend
typeset badrecv=$POOL/badrecv
typeset csrc=$POOL/csrc
typeset esrc=$POOL/esrc
typeset keyfile=$BACKDIR/write-compress.key

function cleanup
{
	datasetexists $badrecv && destroy_dataset $badrecv -r
	datasetexists $csrc && destroy_dataset $csrc -r
	datasetexists $esrc && destroy_dataset $esrc -r
	rm -f $bad $err $good $keyfile
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

log_assert "DRR_WRITE and DRR_SPILL records with a compression type" \
    "that cannot be read back are rejected"

log_must mkdir -p $BACKDIR

typeset wmsg="DRR_WRITE has invalid compression type"
typeset smsg="DRR_SPILL has invalid compression type"
typeset off="must equal logical size"
check_rejected write-compress-on "$wmsg 1"
check_rejected write-compress-off "$wmsg 2"
check_rejected write-compress-empty "$wmsg 4"
check_rejected write-raw-compress-inherit "$wmsg 0"
check_rejected write-raw-compress-on "$wmsg 1"
check_rejected write-raw-compress-off \
    "DRR_WRITE payload size 512 $off 16384 when compression is off"
check_rejected write-raw-compress-empty "$wmsg 4"
check_rejected spill-raw-compress-inherit "$smsg 0"
check_rejected spill-raw-compress-on "$smsg 1"
check_rejected spill-raw-compress-off \
    "DRR_SPILL payload size 512 $off 3072 when compression is off"
check_rejected spill-raw-compress-empty "$smsg 4"

# A genuine compressed stream still receives under that name.
log_must zfs create -o compression=lz4 $csrc
typeset cmnt=$(get_prop mountpoint $csrc)
log_must eval "seq 1 5000 | head -c 16384 >$cmnt/file"
log_must zfs snapshot $csrc@s
log_must eval "zfs send -c $csrc@s >$good"
log_must eval "zstream dump -v $good | \\
    grep -Eq '^WRITE .* compression type = 15 .*'"
log_must eval "zstream dump $good >$err"
log_must eval "zfs recv $badrecv <$good"
log_must cmp $cmnt/file $(get_prop mountpoint $badrecv)/file
log_must zfs destroy -r $badrecv

# So does a genuine raw stream, with OFF records and a compressed spill.
log_must eval "echo 'password' >$keyfile"
log_must zfs create -o compression=lz4 -o xattr=sa -o encryption=on \
    -o keyformat=passphrase -o keylocation=file://$keyfile $esrc
typeset emnt=$(get_prop mountpoint $esrc)
log_must eval "yes 'raw write block' | head -c 16384 >$emnt/file"
typeset xval=$(printf 'spill xattr value %.0s' {1..150})
log_must set_xattr big "$xval" $emnt/file
log_must zfs snapshot $esrc@s
log_must eval "zfs send -w $esrc@s >$good"
log_must eval "zstream dump -v $good | \\
    grep -Eq '^WRITE .* compression type = 2 .*'"
log_must eval "zstream dump -v $good | \\
    grep -Eq '^SPILL .* compression type = 15 .*'"
log_must eval "zstream dump $good >$err"
log_must eval "zfs recv $badrecv <$good"
log_must zfs load-key -L file://$keyfile $badrecv
log_must zfs mount $badrecv
typeset rmnt=$(get_prop mountpoint $badrecv)
log_must cmp $emnt/file $rmnt/file
log_must test "$(get_xattr big $rmnt/file)" = "$xval"

log_pass "DRR_WRITE and DRR_SPILL records with a compression type" \
    "that cannot be read back are rejected"

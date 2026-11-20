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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# The MAC of a block that is authenticated but not encrypted, such as the
# ZPL master node of an encrypted dataset, covers the block as it is stored,
# compressed.  Such a block must stay readable with compressed ARC disabled
# when this host's compressor gives other bytes for it, as for a block
# written on a host of the other byte order.
#
# blockfiles/auth_lz4be.dat.bz2 is a 64M file vdev holding pool lz4be.  The
# block of the ZPL master node of the encrypted filesystem lz4be/enc
# (keyformat=hex, key $KEY) was compressed by an LZ4 whose hash reads
# byte-swapped words, as LZ4 does on a big-endian host, so LZ4 here
# compresses it to other bytes.  The image was written on x86_64 by a
# libzpool program that replaced zfs_lz4_compress() with a copy of
# module/zfs/lz4_zfs.c in which LZ4_HASH_VALUE() and LZ4_HASH64K_VALUE()
# hash the byte-swapped A32(p):
#	pool lz4be on the file, ashift=9, with only the lz4_compress and
#	encryption features and their dependencies enabled
#	lz4be/enc: encryption=aes-256-gcm, keyformat=hex, keylocation=prompt,
#	compression=lz4, holding only the master node, with VERSION and
#	the 1000 entries "file-000000.txt" = 1000 to "file-000999.txt" = 7993
#	snapshot lz4be/enc@snap, then export
#
# zfs send reads VERSION from the master node through the ARC, with the
# snapshot held for decryption, so the ARC checks the MAC of that block.
#
# STRATEGY:
# 1. With compressed ARC enabled, import the pool, load the key and verify
#    that lz4be/enc@snap can be sent.  Export the pool.
# 2. Do the same with compressed ARC disabled, and verify that the pool
#    reports no error.  After the export, send reads the block from disk
#    again, under that setting.
#

verify_runnable "global"

POOL_NAME=lz4be
POOL_FILE=$TEST_BASE_DIR/auth_lz4be.dat
KEY=000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F

function cleanup
{
	poolexists $POOL_NAME && destroy_pool $POOL_NAME
	rm -f $POOL_FILE
	restore_tunable COMPRESSED_ARC_ENABLED
}

function import_and_send
{
	log_must zpool import -N -d $POOL_FILE $POOL_NAME
	log_must eval "echo $KEY | zfs load-key -L prompt $POOL_NAME/enc"
	log_must eval "zfs send $POOL_NAME/enc@snap > /dev/null"
	log_must eval "zpool status -v $POOL_NAME | \
	    grep -q 'No known data errors'"
	log_must zpool export $POOL_NAME
}

log_onexit cleanup

log_assert "Authenticated blocks are read with compressed ARC disabled" \
	"whatever the compressor gives for them"

log_must save_tunable COMPRESSED_ARC_ENABLED
log_must eval "bzcat <" \
	"$STF_SUITE/tests/functional/arc/blockfiles/auth_lz4be.dat.bz2" \
	">$POOL_FILE"

log_must set_tunable32 COMPRESSED_ARC_ENABLED 1
import_and_send

log_must set_tunable32 COMPRESSED_ARC_ENABLED 0
import_and_send

log_pass "Authenticated blocks are read with compressed ARC disabled" \
	"whatever the compressor gives for them"

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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# OpenZFS should be able to heal zstd compressed data using corrective recv
#   from an uncompressed send file.
#
# STRATEGY:
# 0. Create compressible files on zstd compressed datasets, checksum them,
#    then compare the checksums with those obtained after healing:
# 1. For zstd, zstd-12 and zstd-fast-10, test healing recv from an
#    uncompressed send file (the data must be recompressed with the level
#    the block was written with)
# 2. Test healing recv of an encrypted zstd dataset from an uncompressed
#    send file
#

verify_runnable "both"

DISK=${DISKS%% *}

backup=$TEST_BASE_DIR/backup

function cleanup
{
	log_must rm -f $backup

	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must zpool create -f $TESTPOOL $DISK
}

function test_corrective_recv
{
	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -v $TESTPOOL
	log_must eval "zpool status -v $TESTPOOL | \
	    grep \"Permanent errors have been detected\""

	# make sure we will read the corruption from disk by flushing the ARC
	log_must zinject -a

	log_must eval "zfs recv -c $1 < $2"

	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -v $TESTPOOL
	log_mustnot eval "zpool status -v $TESTPOOL | \
	    grep \"Permanent errors have been detected\""
	typeset cksum=$(xxh128digest $file)
	[[ "$cksum" == "$checksum" ]] || \
		log_fail "Checksums differ ($cksum != $checksum)"
}

#
# Create a dataset with the given compression (and any extra options) and
# write a compressible file to it, then snapshot it and generate an
# uncompressed full send file.
#
function setup_dataset # dataset compression [options]
{
	typeset ds=$1
	typeset compress=$2
	shift 2

	log_must zfs create -o primarycache=none -o atime=off \
	    -o compression=$compress "$@" $ds
	file="/$ds/$TESTFILE0"
	log_must eval "dd if=/dev/urandom bs=1024 count=768 | \
	    od -An -tx1 -v > $file"
	log_must sync_pool $TESTPOOL
	checksum=$(xxh128digest $file)

	typeset ratio=$(get_prop compressratio $ds)
	[[ "$ratio" != "1.00x" ]] || \
		log_fail "Data in $ds was not compressed ($ratio)"

	log_must zfs snapshot $ds@snap1
	log_must eval "zfs send $ds@snap1 > $backup"
}

log_onexit cleanup

log_assert "ZFS corrective receive should be able to heal zstd compressed data"

typeset passphrase="password"
typeset file
typeset checksum

log_must eval "poolexists $TESTPOOL && destroy_pool $TESTPOOL"
log_must zpool create -f -o feature@head_errlog=disabled $TESTPOOL $DISK

log_must eval "echo $passphrase > /$TESTPOOL/pwd"

typeset -i i=0
for compress in zstd zstd-12 zstd-fast-10; do
	setup_dataset $TESTPOOL/testfs$i $compress

	corrupt_blocks_at_level $file 0
	# test healing recv from an uncompressed send file
	test_corrective_recv $TESTPOOL/testfs$i@snap1 $backup

	log_must zfs destroy -r $TESTPOOL/testfs$i
	((i = i + 1))
done

# test healing recv of an encrypted dataset using an uncompressed send file
setup_dataset $TESTPOOL/testfs$i zstd-9 -o encryption=aes-256-gcm \
    -o keyformat=passphrase -o keylocation=file:///$TESTPOOL/pwd
corrupt_blocks_at_level $file 0
test_corrective_recv $TESTPOOL/testfs$i@snap1 $backup
log_must zfs destroy -r $TESTPOOL/testfs$i

log_pass "OpenZFS corrective recv works for healing zstd compressed data"

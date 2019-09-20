#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2019 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS be able to heal using corrective recv
#
# STRATEGY:
#  1. Create a dataset
#  2. Snapshot the dataset
#  3. Create a file and get its checksum
#  4. Snapshot the dataset
#  5. Recv dataset into a filesystem with different compression
#  5. Corrupt the file
#  6. Heal the corruption using a corrective send and full send file
#  7. Corrupt the file again
#  8. Heal the corruption using a corrective send an incremental send file
#  9. Corrupt the file again
# 10. Heal the corruption when the target snapshot and the send file have
#     different compressions algorithms
#

verify_runnable "both"

backup=$TEST_BASE_DIR/backup
ibackup=$TEST_BASE_DIR/ibackup.$$

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS2

	for f in $ibackup $backup; do
		[[ -f $f ]] && log_must rm -f $f
	done
}

function test_corrective_recv
{
	# Corrupt all level 0 blocks of the provided file
	corrupt_blocks_at_level $3 0
	log_must zpool scrub $TESTPOOL
	log_must zpool wait -t scrub $TESTPOOL
	log_must eval "zpool status -v $TESTPOOL | \
	    grep \"Permanent errors have been detected\""

	# make sure we will read the corruption from disk by flushing the ARC
	log_must zinject -a

	log_must eval "zfs recv -c $1 < $2"

	log_mustnot eval "zpool status -v $TESTPOOL | \
	    grep \"Permanent errors have been detected\""
	typeset cksum=$(md5digest $file)
	[[ "$cksum" == "$checksum" ]] || \
		log_fail "Checksums differ ($cksum1 != $checksum)"
}

log_onexit cleanup

log_assert "ZFS corrective receive should be able to heal corruption"

typeset snap1="$TESTPOOL/$TESTFS1@snap1"
typeset snap2="$TESTPOOL/$TESTFS1@snap2"
typeset file="/$TESTPOOL/$TESTFS1/$TESTFILE0"

log_must zfs create -o primarycache=none -o recordsize=512 \
    -o compression=lz4 $TESTPOOL/$TESTFS1

log_must zfs snapshot $snap1

log_must dd if=/dev/urandom of=$file bs=1024 count=1024 oflag=sync
typeset checksum=$(md5digest $file)

log_must zfs snapshot $snap2

log_must eval "zfs send $snap2 > $backup"
log_must eval "zfs send -i $snap1 $snap2 > $ibackup"
log_must eval "zfs recv -o compression=gzip -o primarycache=none \
    -o recordsize=512 $TESTPOOL/$TESTFS2 < $backup"

typeset compr=$(get_prop compression $TESTPOOL/$TESTFS2)
[[ "$compr" == "gzip" ]] || \
	log_fail "Unexpected compression $compr in recved dataset"

# test healing recv from a full send file
test_corrective_recv $snap2 $backup $file

# test healing recv from an incremental send file
test_corrective_recv $snap2 $ibackup $file

# test healing recv when compression doesn't match between send file and on-disk
test_corrective_recv "$TESTPOOL/$TESTFS2@snap2" $backup \
    "/$TESTPOOL/$TESTFS2/$TESTFILE0"

log_pass "ZFS corrective recv works for healing"

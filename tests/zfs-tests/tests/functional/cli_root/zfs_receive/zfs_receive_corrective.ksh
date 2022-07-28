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
# Copyright (c) 2022 Axcient.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# OpenZFS should be able to heal data using corrective recv
#
# STRATEGY:
# 0. Create a file, checksum the file to be corrupted then compare it's checksum
#    with the one obtained after healing under different testing scenarios:
# 1. Test healing (aka corrective) recv from a full send file
# 2. Test healing recv (aka heal recv) from an incremental send file
# 3. Test healing recv when compression on-disk is off but source was compressed
# 4. Test heal recv when compression on-disk is on but source was uncompressed
# 5. Test heal recv when compression doesn't match between send file and on-disk
# 6. Test healing recv of an encrypted dataset using an unencrypted send file
# 7. Test healing recv (on an encrypted dataset) using a raw send file
# 8. Test healing when specifying destination filesystem only (no snapshot)
# 9. Test incremental recv aftear healing recv
#

verify_runnable "both"

DISK=${DISKS%% *}

backup=$TEST_BASE_DIR/backup
raw_backup=$TEST_BASE_DIR/raw_backup
ibackup=$TEST_BASE_DIR/ibackup
unc_backup=$TEST_BASE_DIR/unc_backup

function cleanup
{
	log_must rm -f $backup $raw_backup $ibackup $unc_backup

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
	typeset cksum=$(md5digest $file)
	[[ "$cksum" == "$checksum" ]] || \
		log_fail "Checksums differ ($cksum != $checksum)"
}

log_onexit cleanup

log_assert "ZFS corrective receive should be able to heal data corruption"

typeset passphrase="password"
typeset file="/$TESTPOOL/$TESTFS1/$TESTFILE0"

log_must eval "poolexists $TESTPOOL && destroy_pool $TESTPOOL"
log_must zpool create -f -o feature@head_errlog=disabled $TESTPOOL $DISK

log_must eval "echo $passphrase > /$TESTPOOL/pwd"

log_must zfs create -o primarycache=none \
    -o atime=off -o compression=lz4 $TESTPOOL/$TESTFS1

log_must dd if=/dev/urandom of=$file bs=1024 count=1024 oflag=sync
log_must eval "echo 'aaaaaaaa' >> "$file
typeset checksum=$(md5digest $file)

log_must zfs snapshot $TESTPOOL/$TESTFS1@snap1

# create full send file
log_must eval "zfs send $TESTPOOL/$TESTFS1@snap1 > $backup"

log_must dd if=/dev/urandom of=$file"1" bs=1024 count=1024 oflag=sync
log_must eval "echo 'bbbbbbbb' >> "$file"1"
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap2
# create incremental send file
log_must eval "zfs send -i $TESTPOOL/$TESTFS1@snap1 \
    $TESTPOOL/$TESTFS1@snap2 > $ibackup"

corrupt_blocks_at_level $file 0
# test healing recv from a full send file
test_corrective_recv $TESTPOOL/$TESTFS1@snap1 $backup

corrupt_blocks_at_level $file"1" 0
# test healing recv from an incremental send file
test_corrective_recv $TESTPOOL/$TESTFS1@snap2 $ibackup

# create new uncompressed dataset using our send file
log_must eval "zfs recv -o compression=off -o primarycache=none \
    $TESTPOOL/$TESTFS2 < $backup"
typeset compr=$(get_prop compression $TESTPOOL/$TESTFS2)
[[ "$compr" == "off" ]] || \
	log_fail "Unexpected compression $compr in recved dataset"
corrupt_blocks_at_level "/$TESTPOOL/$TESTFS2/$TESTFILE0" 0
# test healing recv when compression on-disk is off but source was compressed
test_corrective_recv "$TESTPOOL/$TESTFS2@snap1" $backup

# create a full sendfile from an uncompressed source
log_must eval "zfs send $TESTPOOL/$TESTFS2@snap1 > $unc_backup"
log_must eval "zfs recv -o compression=gzip -o primarycache=none \
    $TESTPOOL/testfs3 < $unc_backup"
typeset compr=$(get_prop compression $TESTPOOL/testfs3)
[[ "$compr" == "gzip" ]] || \
	log_fail "Unexpected compression $compr in recved dataset"
corrupt_blocks_at_level "/$TESTPOOL/testfs3/$TESTFILE0" 0
# test healing recv when compression on-disk is on but source was uncompressed
test_corrective_recv "$TESTPOOL/testfs3@snap1" $unc_backup

# create new compressed dataset using our send file
log_must eval "zfs recv -o compression=gzip -o primarycache=none \
    $TESTPOOL/testfs4 < $backup"
typeset compr=$(get_prop compression $TESTPOOL/testfs4)
[[ "$compr" == "gzip" ]] || \
	log_fail "Unexpected compression $compr in recved dataset"
corrupt_blocks_at_level "/$TESTPOOL/testfs4/$TESTFILE0" 0
# test healing recv when compression doesn't match between send file and on-disk
test_corrective_recv "$TESTPOOL/testfs4@snap1" $backup

# create new encrypted (and compressed) dataset using our send file
log_must eval "zfs recv -o encryption=aes-256-ccm -o keyformat=passphrase \
    -o keylocation=file:///$TESTPOOL/pwd -o primarycache=none \
    $TESTPOOL/testfs5 < $backup"
typeset encr=$(get_prop encryption $TESTPOOL/testfs5)
[[ "$encr" == "aes-256-ccm" ]] || \
	log_fail "Unexpected encryption $encr in recved dataset"
log_must eval "zfs send --raw $TESTPOOL/testfs5@snap1 > $raw_backup"
log_must eval "zfs send $TESTPOOL/testfs5@snap1 > $backup"
corrupt_blocks_at_level "/$TESTPOOL/testfs5/$TESTFILE0" 0
# test healing recv of an encrypted dataset using an unencrypted send file
test_corrective_recv "$TESTPOOL/testfs5@snap1" $backup
corrupt_blocks_at_level "/$TESTPOOL/testfs5/$TESTFILE0" 0
log_must zfs unmount $TESTPOOL/testfs5
log_must zfs unload-key $TESTPOOL/testfs5
# test healing recv (on an encrypted dataset) using a raw send file
test_corrective_recv "$TESTPOOL/testfs5@snap1" $raw_backup
# non raw send file healing an encrypted dataset with an unloaded key will fail
log_mustnot eval "zfs recv -c $TESTPOOL/testfs5@snap1 < $backup"

log_must zfs rollback -r $TESTPOOL/$TESTFS1@snap1
corrupt_blocks_at_level $file 0
# test healing when specifying destination filesystem only (no snapshot)
test_corrective_recv $TESTPOOL/$TESTFS1 $backup
# test incremental recv aftear healing recv
log_must eval "zfs recv $TESTPOOL/$TESTFS1 < $ibackup"

# test that healing recv can not be combined with incompatible recv options
log_mustnot eval "zfs recv -h -c $TESTPOOL/$TESTFS1@snap1 < $backup"
log_mustnot eval "zfs recv -F -c $TESTPOOL/$TESTFS1@snap1 < $backup"
log_mustnot eval "zfs recv -s -c $TESTPOOL/$TESTFS1@snap1 < $backup"
log_mustnot eval "zfs recv -u -c $TESTPOOL/$TESTFS1@snap1 < $backup"
log_mustnot eval "zfs recv -d -c $TESTPOOL/$TESTFS1@snap1 < $backup"
log_mustnot eval "zfs recv -e -c $TESTPOOL/$TESTFS1@snap1 < $backup"

# ensure healing recv doesn't work when snap GUIDS don't match
log_mustnot eval "zfs recv -c $TESTPOOL/testfs5@snap2 < $backup"
log_mustnot eval "zfs recv -c $TESTPOOL/testfs5 < $backup"

# test that healing recv doesn't work on non-existing snapshots
log_mustnot eval "zfs recv -c $TESTPOOL/$TESTFS1@missing < $backup"

log_pass "OpenZFS corrective recv works for data healing"

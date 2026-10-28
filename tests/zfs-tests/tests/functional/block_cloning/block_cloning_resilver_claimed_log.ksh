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
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A claimed but unreplayed clone record can hold the only reference to
#	a block. A resilver must copy that block.
#
# STRATEGY:
#	1. Snapshot another dataset, then clone an older file into it while
#	   the pool is frozen, so the clone exists only as a record in that
#	   dataset's intent log. The cloned blocks predate the snapshot, below
#	   which a scan of the dataset's own blocks starts.
#	2. Import without mounting, which claims the record, and remove the
#	   source file.
#	3. Replace the only device, then replay the record from the new
#	   device alone and verify the clone.
#	4. Repeat in an encrypted file system with its key unloaded during the
#	   resilver, which then scans the log without decryption.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -rf $workdir
}

log_assert "A resilver copies blocks referenced only by a claimed clone record"
workdir=$(mktemp -d $TEST_BASE_DIR/bclone_claimed_log.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

echo "password" >$workdir/key
for crypt in off on; do
	log_must truncate -s 512M $workdir/disk-0 $workdir/disk-1
	log_must zpool create -o feature@block_cloning=enabled $TESTPOOL1 \
	    $workdir/disk-0
	root=$TESTPOOL1
	if [[ $crypt == on ]]; then
		root=$TESTPOOL1/enc
		log_must zfs create -o encryption=on -o keyformat=passphrase \
		    -o keylocation=file://$workdir/key $root
	fi
	log_must zfs create -o compression=off -o recordsize=128k $root/src
	# Encrypted file systems can clone between each other only when they
	# share a master key, which a clone shares with its origin.
	log_must zfs snapshot $root/src@empty
	log_must zfs clone -o compression=off -o recordsize=128k \
	    $root/src@empty $root/dst
	src=$(get_prop mountpoint $root/src)
	dst=$(get_prop mountpoint $root/dst)
	log_must file_write -o create -f $workdir/expected -b 131072 -c 4 -d 65
	log_must cp $workdir/expected $src/file
	sync_pool $TESTPOOL1
	log_must zfs snapshot $root/dst@snap
	# Records written after the freeze need a ZIL header already on disk.
	log_must dd if=/dev/zero of=$dst/sync conv=fdatasync,fsync bs=1 count=1
	sync_pool $TESTPOOL1

	log_must zpool freeze $TESTPOOL1
	log_must clonefile -f $src/file $dst/clone
	log_must zpool export $TESTPOOL1
	# Frozen labels need -f. Without mounting, the log stays claimed.
	log_must zpool import -f -N -d $workdir $TESTPOOL1
	zdbkey=""
	[[ $crypt == on ]] && zdbkey="-K password"
	# Match a record: the statistics list every type, even with no records.
	log_must eval "zdb $zdbkey -ivv $root/dst |
	    grep -q 'TX_CLONE_RANGE *len'"

	[[ $crypt == on ]] && log_must zfs load-key $root
	log_must zfs mount $root/src
	log_must rm $src/file
	sync_pool $TESTPOOL1
	if [[ $crypt == on ]]; then
		log_must zfs unmount $root/src
		log_must zfs unload-key $root
	fi

	log_must zpool replace -w $TESTPOOL1 $workdir/disk-0 $workdir/disk-1
	log_must zpool export $TESTPOOL1
	log_must rm $workdir/disk-0
	log_must zpool import -l -d $workdir $TESTPOOL1
	log_must cmp $workdir/expected $dst/clone
	log_must zpool destroy $TESTPOOL1
	log_must rm $workdir/disk-1
done

log_pass "A resilver copied blocks referenced only by a claimed clone record"

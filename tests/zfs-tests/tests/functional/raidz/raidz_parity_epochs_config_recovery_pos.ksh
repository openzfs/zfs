#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# https://opensource.org/license/CDDL-1.0.
#
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A native import retains config-only epochs for read-only recovery.
#	Neither a refused writable import nor recovery may change the vdevs.
#
# STRATEGY:
#	Create a filesystem with known data and a config-only table, including
#	a case with metadata born in its new parity epoch.  Refuse writable
#	import, read the filesystem read-only, and compare the complete vdev
#	checksums before and after both operations.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_recovery.$$
typeset -r POOL=${TESTPOOL}_config_recovery
typeset dir mnt phase

typeset -a disks

function cleanup
{
	set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
	poolexists $POOL && log_must zpool export $POOL
	rm -rf $BASEDIR
}

function check_unchanged
{
	log_must eval "cksum $dir/v* > $dir/after.cksum"
	log_must cmp $dir/before.cksum $dir/after.cksum
}

log_onexit cleanup
log_assert "native recovery preserves config-only epochs and filesystem data"
log_must mkdir -p $BASEDIR
log_must dd if=/dev/urandom of=$BASEDIR/expected bs=128k count=64

for phase in future reached; do
	dir=$BASEDIR/$phase
	mnt=$dir/mnt
	log_must mkdir -p $mnt
	for i in 0 1 2 3; do
		disks[i]=$dir/v$i
		log_must truncate -s 256M ${disks[i]}
	done
	log_must zpool create -f -o cachefile=none -O mountpoint=$mnt \
	    -O compression=off $POOL raidz1 ${disks[@]}
	log_must cp $BASEDIR/expected $mnt/payload
	sync_pool $POOL
	log_must zpool export $POOL
	if [[ $phase == reached ]]; then
		log_must raidz_epoch_test label $dir $POOL now
	else
		log_must raidz_epoch_test label $dir $POOL
	fi

	log_must eval "cksum $dir/v* > $dir/before.cksum"
	log_mustnot zpool import -d $dir $POOL
	check_unchanged
	# A config-only table does not activate the feature.  The rescue
	# tunable must still refuse writes that would erase its layout.
	log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 1
	log_mustnot zpool import -d $dir $POOL
	check_unchanged
	log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
	log_must zpool import -o readonly=on -d $dir $POOL
	log_must cmp $BASEDIR/expected $mnt/payload
	log_must zpool export $POOL
	check_unchanged
	log_must eval "zdb -l ${disks[0]} > $dir/labels.out"
	log_must grep -q 'org.openzfs:raidz_parity_epochs\[' $dir/labels.out
done

log_pass "native read-only recovery retains layout, vdev bytes and data"

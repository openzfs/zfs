#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#
# Copyright (c) 2026 Dmitry R.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	RAIDZ children cannot be detached through global contraction controls.
#
# STRATEGY:
#	1. Require the unsafe global contraction controls to be absent.
#	2. Reject detach on a healthy and a degraded four-wide RAIDZ1.
#	3. Verify data before and after reimport, then online and scrub the pool.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_rz_detach
typeset workdir=$TEST_BASE_DIR/raidz_detach.$$
typeset mnt=$workdir/mnt
typeset -a disks

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

log_assert "Healthy and degraded RAIDZ pools reject child detach safely"
log_onexit cleanup

# These parameters must not provide an alternate path around detach safety.
for name in force_width contracting contract_guid; do
	case $UNAME in
	Linux)
		log_mustnot test -e /sys/module/zfs/parameters/raidz_$name
		;;
	FreeBSD)
		log_mustnot sysctl -n vfs.zfs.vdev.$name
		;;
	esac
done

log_must mkdir -p $workdir
for i in 0 1 2 3; do
	disks[i]=$workdir/vdev$i
	log_must truncate -s 256M ${disks[i]}
done
log_must zpool create -f -O mountpoint=$mnt -O compression=off \
    $pool raidz1 ${disks[@]}
log_must dd if=/dev/urandom of=$workdir/expected bs=128k count=64
log_must cp $workdir/expected $mnt/file
sync_pool $pool

log_mustnot zpool detach $pool ${disks[3]}
log_must cmp $workdir/expected $mnt/file
log_must zpool offline $pool ${disks[0]}
log_mustnot zpool detach $pool ${disks[3]}
log_must cmp $workdir/expected $mnt/file
log_must zpool export $pool
log_must zpool import -d $workdir $pool
log_must cmp $workdir/expected $mnt/file
log_must zpool online $pool ${disks[0]}
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"
log_must zpool export $pool
log_must zpool import -d $workdir $pool
log_must cmp $workdir/expected $mnt/file

log_pass "Rejected RAIDZ detaches preserve topology, importability and data"

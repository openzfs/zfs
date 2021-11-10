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
# Copyright (c) 2020 The FreeBSD Foundation [1]
#
# [1] Portions of this software were developed by Allan Jude
#     under sponsorship from the FreeBSD Foundation.

. $STF_SUITE/include/libtest.shlib

export SIZE=1G
export VDIR=$TESTDIR/disk.persist_l2arc
export VDEV="$VDIR/a"
export VDEV_CACHE="$VDIR/b"
export PASSPHRASE="password"

# fio options
export DIRECTORY=/$TESTPOOL-l2arc/encrypted
export NUMJOBS=4
export RUNTIME=30
export PERF_RANDSEED=1234
export PERF_COMPPERCENT=66
export PERF_COMPCHUNK=0
export BLOCKSIZE=128K
export SYNC_TYPE=0
export DIRECT=1

#
# DESCRIPTION:
#	System with compressed_arc disabled succeeds at reading from L2ARC
#
# STRATEGY:
#	1. Disable compressed_arc.
#	2. Create pool with a cache device, encryption, and compression enabled.
#	3. Read the number of L2ARC checksum failures.
#	4. Create a random file in that pool and random read for 30 sec.
#	5. Read the number of L2ARC checksum failures.
#

verify_runnable "global"

log_assert "L2ARC with compressed_arc disabled succeeds."

origin_carc_setting=$(get_tunable COMPRESSED_ARC_ENABLED)

function cleanup
{
	if poolexists $TESTPOOL-l2arc ; then
		destroy_pool $TESTPOOL-l2arc
	fi

	log_must set_tunable64 COMPRESSED_ARC_ENABLED $origin_carc_setting
}
log_onexit cleanup

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must mkfile $SIZE $VDEV

# Disable Compressed ARC so that in-ARC and on-disk will not match
log_must set_tunable64 COMPRESSED_ARC_ENABLED 0

typeset fill_mb=800
typeset cache_sz=$(( floor($fill_mb / 2) ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -O compression=zstd -f $TESTPOOL-l2arc $VDEV cache $VDEV_CACHE

log_must eval "echo $PASSPHRASE | zfs create -o compression=zstd " \
	"-o encryption=on -o keyformat=passphrase -o keylocation=prompt " \
	"$TESTPOOL-l2arc/encrypted"

l2_cksum_bad_start=$(get_arcstat l2_cksum_bad)

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

l2_cksum_bad_end=$(get_arcstat l2_cksum_bad)

log_note "L2ARC Failed Checksums before: $l2_cksum_bad_start After:"\
	"$l2_cksum_bad_end"
log_must test $(( $l2_cksum_bad_end - $l2_cksum_bad_start )) -eq 0

log_must zpool destroy -f $TESTPOOL-l2arc

log_pass "L2ARC with encryption enabled and compressed_arc disabled does not"\
	"result in checksum errors."

#! /bin/ksh -p
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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2020, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#	Resilvering results in no CKSUM errors in pools with indirect vdevs.
#
# STRATEGY:
#	1. Create a pool with two top-vdevs
#	2. Write some files
#	3. Remove one of the top-vdevs
#	4. Reattach it to make a mirror
#

command -v fio > /dev/null || log_unsupported "fio missing"

DISKDIR=$(mktemp -d)
DISK1="$DISKDIR/dsk1"
DISK2="$DISKDIR/dsk2"
DISKS="$DISK1 $DISK2"

# fio options
export DIRECTORY=/$TESTPOOL
export NUMJOBS=16
export RUNTIME=10
export PERF_RANDSEED=1234
export PERF_COMPPERCENT=66
export PERF_COMPCHUNK=0
export BLOCKSIZE=4K
export SYNC_TYPE=0
export DIRECT=1
export FILE_SIZE=128M

log_must mkfile 4g $DISK1
log_must mkfile 4g $DISK2

function cleanup
{
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

log_must zpool create -O recordsize=4k $TESTPOOL $DISK1 $DISK2
log_onexit cleanup

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/sequential_reads.fio

log_must zpool remove -w $TESTPOOL $DISK2
log_must zpool attach -w $TESTPOOL $DISK1 $DISK2

verify_pool $TESTPOOL

log_pass "Resilvering results in no CKSUM errors with indirect vdevs"

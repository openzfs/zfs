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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
# Copyright (c) 2022 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# After filling a filesystem, verify the contents can be removed
# without encountering an ENOSPC error.
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	log_must rm -f $all_vdevs
}

log_onexit cleanup

log_assert "Files can be removed from full file system."

all_vdevs=$(echo $TEST_BASE_DIR/file.{01..12})

log_must truncate -s $MINVDEVSIZE $all_vdevs

log_must zpool create -f $TESTPOOL draid2:8d:2s $all_vdevs
log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must zfs set compression=off $TESTPOOL/$TESTFS

log_note "Writing files until ENOSPC."
log_mustnot_expect "No space left on device" fio --name=test \
    --fallocate=none --rw=write --bs=1M --size=1G --numjobs=4 \
    --sync=1 --directory=$TESTDIR/ --group_reporting

log_must rm $TESTDIR/test.*
log_must test -z "$(ls -A $TESTDIR)"

log_pass "All files removed without error"

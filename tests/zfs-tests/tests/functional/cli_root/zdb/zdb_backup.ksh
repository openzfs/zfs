#!/bin/ksh

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

#
# Copyright (c) 2023, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

write_count=8
blksize=131072

tmpfile=$TEST_BASE_DIR/tmpfile

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm $tmpfile.1 $tmpfile.2
}

log_onexit cleanup

log_assert "Verify that zfs send and zdb -B produce the same stream"

verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
file_write -o create -w -f $TESTDIR/file -b $blksize -c $write_count

snap=$TESTPOOL/$TESTFS@snap
log_must zfs snapshot $snap
typeset -i objsetid=$(zfs get -Ho value objsetid $snap)

sync_pool $TESTPOOL

log_must eval "zfs send -ecL $snap > $tmpfile.1"
log_must eval "zdb -B $TESTPOOL/$objsetid ecL > $tmpfile.2"

typeset sum1=$(cat $tmpfile.1 | md5sum)
typeset sum2=$(cat $tmpfile.2 | md5sum)

log_must test "$sum1" = "$sum2"

log_pass "zfs send and zdb -B produce the same stream"

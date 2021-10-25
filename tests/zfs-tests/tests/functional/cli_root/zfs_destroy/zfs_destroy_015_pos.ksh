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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

# DESCRIPTION
#	zfs destroy <dataset@snap1,snap2..> can destroy a list of multiple
#	snapshots from the same datasets
#
# STRATEGY
#	1. Create multiple snapshots for the same dataset
#	2. Run zfs destroy for these snapshots for a mix of valid and
#	   invalid snapshot names
#	3. Run zfs destroy for snapshots from different datasets and
#	   pools

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1 -R
	datasetexists $TESTPOOL/$TESTFS2 && destroy_dataset $TESTPOOL/$TESTFS2 -R
	poolexists $TESTPOOL2 && zpool destroy $TESTPOOL2
	rm -rf $VIRTUAL_DISK
}

log_assert "zfs destroy for multiple snapshot is handled correctly"
log_onexit cleanup

zfs create $TESTPOOL/$TESTFS1
typeset -i i=1
snaplist=""
log_note "zfs destroy on valid snapshot names"
for i in 1 2 3 4 5; do
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
	snaplist=$snaplist,snap$i
done
snaplist=${snaplist#,}
log_must zfs destroy $TESTPOOL/$TESTFS1@$snaplist
for i in 1 2 3 4 5; do
	log_mustnot snapexists $TESTPOOL/$TESFS1@snap$i
done

log_note "zfs destroy with all bogus snapshot names"
log_mustnot zfs destroy $TESTPOOL/$TESTFS1@snap12,snap21,sna@pple1@,s""nappy2

log_note "zfs destroy with some bogus snapshot names"
for i in 1 2 3; do
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
done
log_must zfs destroy $TESTPOOL/$TESTFS1@snap1,snap2,snapple1,snappy2,snap3
for i in 1 2 3; do
	log_mustnot snapexists $TESTPOOL/$TESTFS1@snap$i
done

log_note "zfs destroy with some snapshot names having special characters"
for i in 1 2 3; do
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
done
log_must zfs destroy $TESTPOOL/$TESTFS1@snap1,@,snap2,,,,snap3,
for i in 1 2 3; do
	log_mustnot snapexists $TESTPOOL/$TESTFS1@snap$i
done
log_note "zfs destroy for too many snapshots"
snaplist=""
for i in {1..100}; do
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
	snaplist=$snaplist,snap$i
done
snaplist=${snaplist#,}
log_must zfs destroy $TESTPOOL/$TESTFS1@$snaplist
for i in {1..100}; do
	log_mustnot snapexists $TESTPOOL/$TESTFS1@snap$i
done
log_note "zfs destroy multiple snapshots with hold"
snaplist=""
for i in 1 2 3 4 5; do
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
	log_must zfs hold keep $TESTPOOL/$TESTFS1@snap$i
	snaplist=$snaplist,snap$i
done
snaplist=${snaplist#,}
log_mustnot zfs destroy $TESTPOOL/$TESTFS1@$snaplist
for i in 1 2 3 4 5; do
	log_must zfs release keep $TESTPOOL/$TESTFS1@snap$i
done
log_must zfs destroy $TESTPOOL/$TESTFS1@$snaplist

log_note "zfs destroy for multiple snapshots having clones"
for i in 1 2 3 4 5; do
	log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
done
snaplist=""
for i in 1 2 3 4 5; do
	log_must zfs clone $TESTPOOL/$TESTFS1@snap$i $TESTPOOL/$TESTFS1/clone$i
	snaplist=$snaplist,snap$i
done
snaplist=${snaplist#,}
log_mustnot zfs destroy $TESTPOOL/$TESTFS1@$snaplist
for i in 1 2 3 4 5; do
	log_must snapexists $TESTPOOL/$TESTFS1@snap$i
	log_must zfs destroy $TESTPOOL/$TESTFS1/clone$i
done

log_note "zfs destroy for snapshots for different datasets"
log_must zfs create $TESTPOOL/$TESTFS2
log_must zfs snapshot $TESTPOOL/$TESTFS2@fs2snap
log_must zfs create $TESTPOOL/$TESTFS1/$TESTFS2
log_must zfs snapshot $TESTPOOL/$TESTFS1/$TESTFS2@fs12snap

long_arg=$TESTPOOL/$TESTFS1@snap1,$TESTPOOL/$TESTFS2@fs2snap,
long_arg=$long_arg$TESTPOOL/$TESTFS1/$TESTFS2@fs12snap
log_must zfs destroy $long_arg
log_mustnot snapexists $TESTPOOL/$TESTFS1@snap1
log_must snapexists $TESTPOOL/$TESTFS2@fs2snap
log_must snapexists $TESTPOOL/$TESTFS1/$TESTFS2@fs12snap

log_must zfs destroy $TESTPOOL/$TESTFS1@fs2snap,fs12snap,snap2
log_must snapexists $TESTPOOL/$TESTFS2@fs2snap
log_must snapexists $TESTPOOL/$TESTFS1/$TESTFS2@fs12snap
log_mustnot snapexists $TESTPOOL/$TESTFS1@snap2

log_must zfs destroy $TESTPOOL/$TESTFS2@fs2snap,fs12snap,snap3
log_mustnot snapexists $TESTPOOL/$TESTFS2@fs2snap
log_must snapexists $TESTPOOL/$TESTFS1/$TESTFS2@fs12snap
log_must snapexists $TESTPOOL/$TESTFS1@snap3

log_note "zfs destroy for snapshots from different pools"
VIRTUAL_DISK=$TEST_BASE_DIR/disk
log_must mkfile $MINVDEVSIZE $VIRTUAL_DISK
log_must zpool create $TESTPOOL2 $VIRTUAL_DISK
log_must poolexists $TESTPOOL2

log_must zfs create $TESTPOOL2/$TESTFS1
log_must zfs snapshot $TESTPOOL2/$TESTFS1@snap
long_arg=$TESTPOOL2/$TESTFS1@snap,$TESTPOOL/$TESTFS1@snap3,
long_arg=$long_arg$TESTPOOL/$TESTFS1@snap5
log_must zfs destroy $long_arg
log_mustnot snapexists $TESTPOOL2/$TESTFS1@snap
log_must snapexists $TESTPOOL/$TESTFS1@snap3
log_must snapexists $TESTPOOL/$TESTFS1@snap5

log_must zfs snapshot $TESTPOOL2/$TESTFS1@snap
log_must zfs destroy $TESTPOOL2/$TESTFS1@snap5,snap3,snap
log_mustnot snapexists $TESTPOOL2/$TESTFS1@snap
log_must snapexists $TESTPOOL/$TESTFS1@snap3
log_must snapexists $TESTPOOL/$TESTFS1@snap5

log_pass "zfs destroy for multiple snapshots passes"

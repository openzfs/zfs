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
# Copyright (c) 2018 by Nutanix. All rights reserved.
# Copyright 2019 Richard Elling
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -d will work on imported/exported pool with pool/dataset argument
#
# Strategy:
# 1. Create a pool
# 2. Run zdb -d with pool and dataset arguments.
# 3. Export the pool
# 4. Run zdb -ed with pool and dataset arguments.
#

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	for DISK in $DISKS; do
		zpool labelclear -f $DEV_RDSKDIR/$DISK
	done
}

log_assert "Verify zdb -d works on imported/exported pool with pool/dataset argument"
log_onexit cleanup

verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
log_must zfs snap $TESTPOOL/$TESTFS@snap

log_must zdb -d $TESTPOOL
log_must zdb --dataset $TESTPOOL/
log_must zdb -d $TESTPOOL/$TESTFS
log_must zdb -d $TESTPOOL/$TESTFS@snap

# more detailed dataset information and zdb long option tests
typeset tmpfile=$(mktemp)
log_must zdb --dataset $TESTPOOL/$TESTFS >$tmpfile
log_mustnot grep -q "DMU dnode" $tmpfile
log_must zdb --dataset --dataset $TESTPOOL/$TESTFS >$tmpfile
log_must grep -q "DMU dnode" $tmpfile
rm -f $tmpfile

# in many test systems, $TESTPOOL labels may linger
# to avoid zdb import confusion on exported pool names, use pool guid
TESTPOOL_GUID=$(zpool get -Hp -o value guid $TESTPOOL)
log_must zpool export $TESTPOOL

# to use the '-e' option, the DISKS need to be in /dev
# check the dirname to see if they are elsewhere
DISKS_ARRAY=($DISKS)
DIRNAME=$(dirname ${DISKS_ARRAY[0]})
[[ $DIRNAME == "." ]] && DIRNAME=/dev

log_must zdb -ed -p $DIRNAME $TESTPOOL_GUID
log_must zdb -ed -p $DIRNAME $TESTPOOL_GUID/
log_must zdb --exported --dataset --path=$DIRNAME $TESTPOOL_GUID/$TESTFS
log_must zdb -e --dataset --path $DIRNAME $TESTPOOL_GUID/$TESTFS
log_must zdb -ed -p $DIRNAME $TESTPOOL_GUID/$TESTFS@snap

log_must zpool import $TESTPOOL

cleanup

log_pass "zdb -d works on imported/exported pool with pool/dataset argument"

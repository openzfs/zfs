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
# Copyright 2016, loli10K. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Verify ZFS successfully receive and restore properties.
#
# STRATEGY:
# 1. Create a filesystem.
# 2. Create a full stream with properties and receive it.
# 3. Create also an incremental stream without some properties and a truncated
#    stream.
# 4. Fail to receive the truncated incremental stream and verify previously
#    received properties are still present.
# 5. Receive the complete incremental send stream and verify that sent
#    properties are successfully received.
#

verify_runnable "both"

orig=$TESTPOOL/$TESTFS1
dest=$TESTPOOL/$TESTFS2
typeset userprop=$(valid_user_property 8)
typeset userval=$(user_property_value 8)
typeset streamfile_full=$TESTDIR/streamfile_full.$$
typeset streamfile_incr=$TESTDIR/streamfile_incr.$$
typeset streamfile_trun=$TESTDIR/streamfile_trun.$$

function cleanup
{
	log_must rm $streamfile_full
	log_must rm $streamfile_incr
	log_must rm $streamfile_trun
	log_must zfs destroy -rf $orig
	log_must zfs destroy -rf $dest
}

#
# Verify property $2 is set from source $4 on dataset $1 and has value $3.
#
# $1 checked dataset
# $2 user property
# $3 property value
# $4 source
#
function check_prop_source
{
	typeset dataset=$1
	typeset prop=$2
	typeset value=$3
	typeset source=$4
	typeset chk_value=$(get_prop "$prop" "$dataset")
	typeset chk_source=$(get_source "$prop" "$dataset")
	if [[ "$chk_value" != "$value" || \
	    "$chk_source" != "$4" ]]
	then
		return 1
	else
		return 0
	fi
}

log_assert "ZFS successfully receive and restore properties."
log_onexit cleanup

# 1. Create a filesystem.
log_must eval "zfs create $orig"
mntpnt=$(get_prop mountpoint $orig)

# 2. Create a full stream with properties and receive it.
log_must eval "zfs set compression='gzip-1' $orig"
log_must eval "zfs set '$userprop'='$userval' $orig"
log_must eval "zfs snapshot $orig@snap1"
log_must eval "zfs send -p $orig@snap1 > $streamfile_full"
log_must eval "zfs recv $dest < $streamfile_full"
log_must eval "check_prop_source $dest compression 'gzip-1' received"
log_must eval "check_prop_source $dest '$userprop' '$userval' received"

# 3. Create also an incremental stream without some properties and a truncated
#    stream.
log_must eval "zfs set compression='gzip-2' $orig"
log_must eval "zfs inherit '$userprop' $orig"
log_must eval "dd if=/dev/urandom of=$mntpnt/file bs=1024k count=10"
log_must eval "zfs snapshot $orig@snap2"
log_must eval "zfs send -p -i $orig@snap1 $orig@snap2 > $streamfile_incr"
log_must eval "dd if=$streamfile_incr of=$streamfile_trun bs=1024k count=9"
log_must eval "zfs snapshot $orig@snap3"
log_must eval "zfs send -p -i $orig@snap1 $orig@snap3 > $streamfile_incr"

# 4. Fail to receive the truncated incremental stream and verify previously
#    received properties are still present.
log_mustnot eval "zfs recv -F $dest < $streamfile_trun"
log_must eval "check_prop_source $dest compression 'gzip-1' received"
log_must eval "check_prop_source $dest '$userprop' '$userval' received"

# 5. Receive the complete incremental send stream and verify that sent
#    properties are successfully received.
log_must eval "zfs recv -F $dest < $streamfile_incr"
log_must eval "check_prop_source $dest compression 'gzip-2' received"
log_must eval "check_prop_source $dest '$userprop' '-' '-'"

log_pass "ZFS properties are successfully received and restored."

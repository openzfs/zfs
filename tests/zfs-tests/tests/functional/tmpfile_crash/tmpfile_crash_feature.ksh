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

. $STF_SUITE/tests/functional/tmpfile_crash/tmpfile_crash.kshlib

#
# DESCRIPTION:
#	The ziltmpfile feature becomes active only for a dataset that logs
#	an O_TMPFILE publication, not for every intent log, and returns to
#	enabled when that dataset's log is gone.
#
# STRATEGY:
#	1. Create a pool on a file vdev with two file systems.
#	2. fsync a named file in the first: ziltmpfile must stay enabled.
#	3. Publish an O_TMPFILE file in the second, as its first log commit,
#	   which creates its log: ziltmpfile must become active.
#	4. Destroy the second file system: ziltmpfile must be enabled again.
#	5. Publish in the first, whose log already exists without the
#	   feature: ziltmpfile must become active.
#

verify_runnable "global"

function feature_state
{
	get_pool_prop feature@ziltmpfile $TESTPOOL
}

function expect_feature # state when
{
	typeset got=$(feature_state)

	[[ "$got" == "$1" ]] || log_fail "ziltmpfile is $got $2, expected $1"
	log_note "ziltmpfile is $got $2"
}

log_assert "ziltmpfile is active only for datasets that log a publication"
log_onexit tmpfile_crash_cleanup

if ! zpool upgrade -v | grep -qw ziltmpfile; then
	log_unsupported "the ziltmpfile feature is not supported"
fi

typeset vdev=$TMPFILE_CRASH_DIR/vdev

log_must rm -rf $TMPFILE_CRASH_DIR
log_must mkdir -p $TMPFILE_CRASH_DIR
log_must truncate -s $MINVDEVSIZE $vdev
log_must zpool create -o cachefile=none -O xattr=sa $TESTPOOL $vdev
log_must zfs create $TESTPOOL/named
log_must zfs create $TESTPOOL/tmpfile
expect_feature enabled "on a new pool"

log_must dd if=/dev/urandom of=$(get_prop mountpoint $TESTPOOL/named)/f \
    bs=128k count=1 conv=fsync
log_must zpool sync $TESTPOOL
expect_feature enabled "after an fsync of a named file"

log_must tmpfile_crash publish $(get_prop mountpoint $TESTPOOL/tmpfile) f
expect_feature active "after a logged publication on a new log"

log_must zfs destroy $TESTPOOL/tmpfile
log_must zpool sync $TESTPOOL
expect_feature enabled "after destroying the publishing file system"

log_must tmpfile_crash publish $(get_prop mountpoint $TESTPOOL/named) g
expect_feature active "after a logged publication on an existing log"

log_pass "ziltmpfile is active only for datasets that log a publication"

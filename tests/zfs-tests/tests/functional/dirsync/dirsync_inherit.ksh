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

. $STF_SUITE/tests/functional/dirsync/dirsync.kshlib

#
# DESCRIPTION:
# The dirsync property is inherited and is kept by zfs send -p.
#
# STRATEGY:
# 1. Set dirsync=on on a file system and create a child.
# 2. Verify that the child inherits it and that its namespace
#    operations commit the ZIL.
# 3. Override it on the child, then inherit it again.
# 4. Send the parent with -p and receive it.
# 5. Verify that the received file system has dirsync=on and that its
#    namespace operations commit the ZIL.
#

verify_runnable "global"

PARENT=$TESTPOOL/$TESTFS/parent
CHILD=$PARENT/child
RECV=$TESTPOOL/$TESTFS/recv

function cleanup
{
	datasetexists $RECV && destroy_dataset $RECV -r
	datasetexists $PARENT && destroy_dataset $PARENT -r
}

function check_dirsync # dataset value source
{
	typeset actual=$(zfs get -H -o value,source dirsync $1)

	if [[ "$actual" != "$2	$3" ]]; then
		log_fail "$1: dirsync is '$actual', expected '$2	$3'"
	fi
}

log_assert "dirsync is inherited and kept by zfs send -p"
log_onexit cleanup

log_must zfs create -o dirsync=on $PARENT
log_must zfs create $CHILD
check_dirsync $CHILD on "inherited from $PARENT"
dirsync_ops $CHILD $(get_prop mountpoint $CHILD) yes

log_must zfs set dirsync=off $CHILD
check_dirsync $CHILD off local
dirsync_ops $CHILD $(get_prop mountpoint $CHILD) no

log_must zfs inherit dirsync $CHILD
check_dirsync $CHILD on "inherited from $PARENT"

log_must zfs snapshot $PARENT@snap
log_must eval "zfs send -p $PARENT@snap | zfs receive $RECV"
check_dirsync $RECV on received
dirsync_ops $RECV $(get_prop mountpoint $RECV) yes

log_pass "dirsync is inherited and kept by zfs send -p"

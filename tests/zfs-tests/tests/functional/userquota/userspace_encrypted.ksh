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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
# 'zfs userspace' and 'zfs groupspace' can be used on encrypted datasets
#
#
# STRATEGY:
# 1. Create both un-encrypted and encrypted datasets
# 2. Receive un-encrypted dataset in encrypted hierarchy
# 3. Verify encrypted datasets support 'zfs userspace' and 'zfs groupspace'
#

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

function log_must_unsupported
{
	log_must_retry "unsupported" 3 "$@" || log_fail
}

log_onexit cleanup

FILEDEV="$TEST_BASE_DIR/userspace_encrypted"
POOLNAME="testpool$$"
typeset -a POOL_OPTS=('' # all pool features enabled
    '-d' # all pool features disabled
    '-d -o feature@userobj_accounting=enabled' # only userobj_accounting enabled
    '-d -o feature@project_quota=enabled') # only project_quota enabled
DATASET_ENCROOT="$POOLNAME/encroot"
DATASET_SENDFS="$POOLNAME/sendfs"

log_assert "'zfs user/groupspace' should work on encrypted datasets"

for opts in "${POOL_OPTS[@]}"; do
	# Setup
	truncate -s $SPA_MINDEVSIZE $FILEDEV
	log_must zpool create $opts -o feature@encryption=enabled $POOLNAME \
		$FILEDEV

	# 1. Create both un-encrypted and encrypted datasets
	log_must zfs create $DATASET_SENDFS
	log_must eval "echo 'password' | zfs create -o encryption=on" \
		"-o keyformat=passphrase -o keylocation=prompt " \
		"$DATASET_ENCROOT"
	log_must zfs create $DATASET_ENCROOT/fs

	# 2. Receive un-encrypted dataset in encrypted hierarchy
	log_must zfs snap $DATASET_SENDFS@snap
	log_must eval "zfs send $DATASET_SENDFS@snap | zfs recv " \
		"$DATASET_ENCROOT/recvfs"

	# 3. Verify encrypted datasets support 'zfs userspace' and
	# 'zfs groupspace'
	log_must zfs userspace $DATASET_ENCROOT/fs
	log_must zfs groupspace $DATASET_ENCROOT/fs
	log_must_unsupported zfs userspace $DATASET_ENCROOT/recvfs
	log_must_unsupported zfs groupspace $DATASET_ENCROOT/recvfs

	# Cleanup
	cleanup
done

log_pass "'zfs user/groupspace' works on encrypted datasets"

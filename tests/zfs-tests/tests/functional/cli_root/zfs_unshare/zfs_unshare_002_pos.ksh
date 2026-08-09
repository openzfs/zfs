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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zfs unshare [-a] <filesystem|mountpoint>' is aware of legacy share.
#
# STRATEGY:
# 1. Set 'zfs set sharenfs=off'
# 2. Use 'share' to share given filesystem
# 3. Verify that 'zfs unshare <filesystem|mountpoint>' is aware of legacy share
# 4. Verify that 'zfs unshare -a' is aware of legacy share.
#

verify_runnable "global"

if is_linux; then
	log_unsupported "zfs set sharenfs=off won't unshare if already off"
fi

function cleanup
{
	typeset -i i=0
	while (( i < ${#mntp_fs[*]} )); do
		is_shared ${mntp_fs[i]} && \
			log_must eval "unshare_nfs ${mntp_fs[i]}"

		((i = i + 2))
	done

	if mounted $TESTPOOL/$TESTCLONE; then
		log_must zfs unmount $TESTDIR2
	fi

	[[ -d $TESTDIR2 ]] && \
		log_must rm -rf $TESTDIR2

	datasetexists "$TESTPOOL/$TESTCLONE" && \
		destroy_dataset $TESTPOOL/$TESTCLONE -f

	snapexists "$TESTPOOL/$TESTFS2@snapshot" && \
		destroy_dataset $TESTPOOL/$TESTFS2@snapshot -f

	datasetexists "$TESTPOOL/$TESTFS2" && \
		destroy_dataset $TESTPOOL/$TESTFS2 -f
}

#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# to verify 'zfs unshare' is aware of legacy share.
#
function test_legacy_unshare # <mntp> <filesystem>
{
        typeset mntp=$1
        typeset filesystem=$2

	log_must zfs set sharenfs=off $filesystem
	not_shared $mntp || \
	    log_fail "'zfs set sharenfs=off' fails to make ZFS " \
	    "filesystem $filesystem unshared."

	log_must share_nfs $mntp
	is_shared $mntp || \
	    log_fail "'share' command fails to share ZFS file system."
	#
	# Verify 'zfs unshare <filesystem>' is aware of legacy share.
	#
	log_mustnot zfs unshare $filesystem
	is_shared $mntp || \
	    log_fail "'zfs unshare <filesystem>' fails to be aware" \
	    "of legacy share."

	#
	# Verify 'zfs unshare <filesystem>' is aware of legacy share.
	#
	log_mustnot zfs unshare $mntp
	is_shared $mntp || \
	    log_fail "'zfs unshare <mountpoint>' fails to be aware" \
	    "of legacy share."
}


set -A mntp_fs \
    "$TESTDIR" "$TESTPOOL/$TESTFS" \
    "$TESTDIR1" "$TESTPOOL/$TESTCTR/$TESTFS1" \
    "$TESTDIR2" "$TESTPOOL/$TESTCLONE"

log_assert "Verify that 'zfs unshare [-a]' is aware of legacy share."
log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS2
log_must zfs snapshot $TESTPOOL/$TESTFS2@snapshot
log_must zfs clone $TESTPOOL/$TESTFS2@snapshot $TESTPOOL/$TESTCLONE
log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL/$TESTCLONE

#
# Invoke 'test_legacy_unshare' routine to verify.
#
typeset -i i=0
while (( i < ${#mntp_fs[*]} )); do
	test_legacy_unshare ${mntp_fs[i]} ${mntp_fs[((i + 1 ))]}

	((i = i + 2))
done


log_note "Verify 'zfs unshare -a' is aware of legacy share."

#
# set the 'sharenfs' property to 'off' for each filesystem
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        log_must zfs set sharenfs=off ${mntp_fs[((i + 1))]}
        not_shared ${mntp_fs[i]} || \
                log_fail "'zfs set sharenfs=off' unshares file system failed."

        ((i = i + 2))
done

#
# Share each of the file systems via legacy share.
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        share_nfs ${mntp_fs[i]}
        is_shared ${mntp_fs[i]} || \
                log_fail "'share' shares ZFS filesystem failed."

        ((i = i + 2))
done

#
# Verify that 'zfs unshare -a' is aware of legacy share
#
log_must zfs unshare -a

#
# verify ZFS filesystems are still shared
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        is_shared ${mntp_fs[i]} || \
            log_fail "'zfs  unshare -a' fails to be aware of legacy share."

        ((i = i + 2))
done

log_pass "'zfs unshare [-a]' succeeds to be aware of legacy share."

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
# Verify that 'zfs unshare <filesystem|mountpoint>' unshares a given shared
# filesystem.
#
# STRATEGY:
# 1. Share filesystems
# 2. Invoke 'zfs unshare <filesystem|mountpoint>' to unshare zfs file system
# 3. Verify that the file system is unshared
# 4. Verify that unsharing an unshared file system fails
# 5. Verify that "zfs unshare -a" succeeds to unshare all zfs file systems.
#

verify_runnable "global"

function cleanup
{
	typeset -i i=0
	while (( i < ${#mntp_fs[*]} )); do
		log_must zfs set sharenfs=off ${mntp_fs[((i+1))]}

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
# unshare the filesystem via <filesystem|mountpoint> argument
# and then verify it has been unshared.
#
function test_unshare # <mntp> <filesystem>
{
        typeset mntp=$1
        typeset filesystem=$2
	typeset prop_value

	prop_value=$(get_prop "sharenfs" $filesystem)

	if [[ $prop_value == "off" ]]; then
		not_shared $mntp ||
			log_must eval "unshare_nfs $mntp"
		log_must zfs set sharenfs=on $filesystem
		is_shared $mntp || \
			log_fail "'zfs set sharenfs=on' fails to make" \
				"file system $filesystem shared."
	fi

	is_shared $mntp || log_must zfs share $filesystem

        #
	# Verify 'zfs unshare <filesystem>' works as well.
	#
	log_must zfs unshare $filesystem
	not_shared $mntp || log_fail "'zfs unshare <filesystem>' fails"

	log_must zfs share $filesystem

	log_must zfs unshare $mntp
	not_shared $mntp || log_fail "'zfs unshare <mountpoint>' fails"

        log_note "Unsharing an unshared file system fails."
        log_mustnot zfs unshare $filesystem
	log_mustnot zfs unshare $mntp
}

set -A mntp_fs \
    "$TESTDIR" "$TESTPOOL/$TESTFS" \
    "$TESTDIR1" "$TESTPOOL/$TESTCTR/$TESTFS1" \
    "$TESTDIR2" "$TESTPOOL/$TESTCLONE"

log_assert "Verify that 'zfs unshare [-a] <filesystem|mountpoint>' succeeds as root."
log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS2
log_must zfs snapshot $TESTPOOL/$TESTFS2@snapshot
log_must zfs clone $TESTPOOL/$TESTFS2@snapshot $TESTPOOL/$TESTCLONE
log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL/$TESTCLONE

#
# Invoke 'test_unshare' routine to test 'zfs unshare <filesystem|mountpoint>'.
#
typeset -i i=0
while (( i < ${#mntp_fs[*]} )); do
	test_unshare ${mntp_fs[i]} ${mntp_fs[((i + 1 ))]}

	((i = i + 2))
done

log_note "Verify 'zfs unshare -a' succeeds as root."

i=0
typeset sharenfs_val
while (( i < ${#mntp_fs[*]} )); do
	sharenfs_val=$(get_prop "sharenfs" ${mntp_fs[((i+1))]})
	if [[ $sharenfs_val == "on" ]]; then
		not_shared ${mntp_fs[i]} && \
			log_must zfs share ${mntp_fs[((i+1))]}
	else
		log_must zfs set sharenfs=on ${mntp_fs[((i+1))]}
		is_shared ${mntp_fs[i]} || \
			log_fail "'zfs set sharenfs=on' fails to share filesystem: ${mntp_fs[i]} not shared."
	fi

        ((i = i + 2))
done

#
# test 'zfs unshare -a '
#
log_must zfs unshare -a

#
# verify all shared filesystems become unshared
#
i=0
while (( i < ${#mntp_fs[*]} )); do
        not_shared ${mntp_fs[i]} || \
                log_fail "'zfs unshare -a' fails to unshare all shared zfs filesystems: ${mntp_fs[i]} still shared."

        ((i = i + 2))
done

log_pass "'zfs unshare [-a] <filesystem|mountpoint>' succeeds as root."

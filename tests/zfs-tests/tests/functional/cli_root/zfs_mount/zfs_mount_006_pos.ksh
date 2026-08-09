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
# Copyright (c) 2018, Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
#	Invoke "zfs mount <filesystem>" with a filesystem mountpoint that is
#	identical to an existing one.  It will fail with a return code of 1
#	when overlay=off.  Place a file in the directory to ensure the failure.
#	Also test overlay=on (default) in which case the mount will not fail.
#
# STRATEGY:
#	1. Prepare an existing mounted filesystem.
#	2. Setup a new filesystem with overlay=off and make sure that it is
#	   unmounted.
#	3. Place a file in the mount point folder.
#	4. Mount the new filesystem using the various combinations
#	   - zfs set mountpoint=<identical path> <filesystem>
#	   - zfs set mountpoint=<top path> <filesystem>
#	5. Verify that mount failed with return code of 1.
#	6. For Linux, also set overlay=on and verify the mount is
#	   allowed.
#

verify_runnable "both"

function cleanup
{
	log_must force_unmount $TESTPOOL/$TESTFS

	datasetexists $TESTPOOL/$TESTFS1 && \
		cleanup_filesystem $TESTPOOL $TESTFS1

	[[ -d $TESTDIR ]] && \
		log_must rm -rf $TESTDIR
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must force_unmount $TESTPOOL/$TESTFS

	return 0
}

typeset -i ret=0

log_assert "Verify that 'zfs $mountcmd <filesystem>'" \
	"where the mountpoint is identical or on top of an existing one" \
	"will fail with return code 1 when overlay=off."

log_onexit cleanup

unmounted $TESTPOOL/$TESTFS || \
	log_must force_unmount $TESTPOOL/$TESTFS

[[ -d $TESTDIR ]] && \
	log_must rm -rf $TESTDIR

typeset -i MAXDEPTH=3
typeset -i depth=0
typeset mtpt=$TESTDIR

while (( depth < MAXDEPTH )); do
	mtpt=$mtpt/$depth
	(( depth = depth + 1))
done

log_must zfs set mountpoint=$mtpt $TESTPOOL/$TESTFS
log_must ismounted $TESTPOOL/$TESTFS

log_must zfs set overlay=off $TESTPOOL/$TESTFS
if ! is_illumos; then
	touch $mtpt/file.1
	log_must ls -l $mtpt | grep file
fi

mounted $TESTPOOL/$TESTFS || \
	log_unresolved "Filesystem $TESTPOOL/$TESTFS is unmounted"

log_must zfs create -o overlay=off $TESTPOOL/$TESTFS1

unmounted $TESTPOOL/$TESTFS1 || \
	log_must force_unmount $TESTPOOL/$TESTFS1

while [[ $depth -gt 0 ]] ; do
	(( depth == MAXDEPTH )) && \
		log_note "Verify that 'zfs $mountcmd <filesystem>'" \
		"with a mountpoint that is identical to an existing one" \
		"will fail with return code 1."

	log_must zfs set mountpoint=$mtpt $TESTPOOL/$TESTFS1
	log_note "zfs $mountcmd $TESTPOOL/$TESTFS1"

	log_mustnot zfs $mountcmd $TESTPOOL/$TESTFS1

	if ! is_illumos; then
		# Test the overlay=on feature which allows
		# mounting of non-empty directory.
		log_must zfs set overlay=on $TESTPOOL/$TESTFS1
		log_must zfs $mountcmd $TESTPOOL/$TESTFS1
		log_must force_unmount $TESTPOOL/$TESTFS1
		log_must zfs set overlay=off $TESTPOOL/$TESTFS1
	fi

	unmounted $TESTPOOL/$TESTFS1 || \
		log_fail "Filesystem $TESTPOOL/$TESTFS1 is mounted."

	mtpt=${mtpt%/*}

	(( depth == MAXDEPTH )) && \
		log_note "Verify that 'zfs $mountcmd <filesystem>'" \
		"with a mountpoint on top of an existing one" \
		"will fail with return code 1."
	(( depth = depth - 1 ))
done

log_pass "'zfs $mountcmd <filesystem>'" \
	"with a mountpoint that is identical or on top of an existing one" \
	"will fail with return code 1."

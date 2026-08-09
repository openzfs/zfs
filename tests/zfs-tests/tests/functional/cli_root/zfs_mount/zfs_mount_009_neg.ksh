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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
#	Try each 'zfs mount' with inapplicable scenarios to make sure
#	it returns an error. include:
#		* '-a', but also with a specific filesystem.
#
# STRATEGY:
#	1. Create an array of parameters
#	2. For each parameter in the array, execute the sub-command
#	3. Verify an error is returned.
#

verify_runnable "both"

multifs="$TESTFS $TESTFS1"
datasets=""

for fs in $multifs ; do
	datasets="$datasets $TESTPOOL/$fs"
done

set -A args "$mountall $TESTPOOL/$TESTFS"

function setup_all
{
	typeset fs

	for fs in $multifs ; do
		setup_filesystem "$DISKS" "$TESTPOOL" \
			"$fs" \
			"${TEST_BASE_DIR%%/}/testroot$$/$TESTPOOL/$fs"
	done
	return 0
}

function cleanup_all
{
	typeset fs

	cleanup_filesystem "$TESTPOOL" "$TESTFS1"
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	[[ -d ${TEST_BASE_DIR%%/}/testroot$$ ]] && \
		rm -rf ${TEST_BASE_DIR%%/}/testroot$$


	return 0
}

function verify_all
{
	typeset fs

	for fs in $multifs ; do
		log_must unmounted $TESTPOOL/$fs
	done
	return 0
}

log_assert "Badly-formed 'zfs $mountcmd' with inapplicable scenarios " \
	"should return an error."
log_onexit cleanup_all

log_must setup_all

log_must zfs $unmountall

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zfs ${args[i]}
	((i = i + 1))
done

log_must verify_all

log_pass "Badly formed 'zfs $mountcmd' with inapplicable scenarios " \
	"fail as expected."

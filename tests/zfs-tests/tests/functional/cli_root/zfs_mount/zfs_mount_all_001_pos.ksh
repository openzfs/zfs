#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

# DESCRIPTION:
#       Verify that 'zfs mount -a' succeeds as root.
#
# STRATEGY:
#       1. Create a group of pools with specified vdev.
#       2. Create zfs filesystems within the given pools.
#       3. Unmount all the filesystems.
#       4. Verify that 'zfs mount -a' command succeed,
#	   and all available ZFS filesystems are mounted.
#	5. Verify that 'zfs mount' is identical with 'df -F zfs'
#

verify_runnable "both"

set -A fs "$TESTFS" "$TESTFS1"
set -A ctr "" "$TESTCTR" "$TESTCTR/$TESTCTR1" "$TESTCTR1"
set -A vol "$TESTVOL" "$TESTVOL1"

function setup_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path

	while (( i < ${#ctr[*]} )); do

		path=${TEST_BASE_DIR%%/}/testroot$$/$TESTPOOL
		if [[ -n ${ctr[i]} ]]; then
			path=$path/${ctr[i]}

			setup_filesystem "$DISKS" "$TESTPOOL" \
				"${ctr[i]}" "$path" \
				"ctr"
		fi

		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				setup_filesystem "$DISKS" "$TESTPOOL" \
					"${ctr[i]}/${vol[j]}" \
					"$path/${vol[j]}" \
					"vol"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			setup_filesystem "$DISKS" "$TESTPOOL" \
				"${ctr[i]}/${fs[j]}" \
				"$path/${fs[j]}"
			((j = j + 1))
		done

		((i = i + 1))
	done

	return 0
}

function cleanup_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path

	((i = ${#ctr[*]} - 1))

	while (( i >= 0 )); do
		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				cleanup_filesystem "$TESTPOOL" \
					"${ctr[i]}/${vol[j]}"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			cleanup_filesystem "$TESTPOOL" \
				"${ctr[i]}/${fs[j]}"
			((j = j + 1))
		done

		[[ -n ${ctr[i]} ]] && \
			cleanup_filesystem "$TESTPOOL" "${ctr[i]}"

		((i = i - 1))
	done

	[[ -d ${TEST_BASE_DIR%%/}/testroot$$ ]] && \
		rm -rf ${TEST_BASE_DIR%%/}/testroot$$
}

#
# This function takes a single true/false argument. If true it will verify that
# all file systems are mounted. If false it will verify that they are not
# mounted.
#
function verify_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path
	typeset logfunc

	if $1; then
		logfunc=log_must
	else
		logfunc=log_mustnot
	fi

	while (( i < ${#ctr[*]} )); do

		path=$TESTPOOL
		[[ -n ${ctr[i]} ]] && \
			path=$path/${ctr[i]}

		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				log_mustnot mounted "$path/${vol[j]}"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			$logfunc mounted "$path/${fs[j]}"
			((j = j + 1))
		done

		$logfunc mounted "$path"

		((i = i + 1))
	done

	return 0
}


log_assert "Verify that 'zfs $mountall' succeeds as root, " \
	"and all available ZFS filesystems are mounted."

log_onexit cleanup_all

log_must setup_all

export __ZFS_POOL_RESTRICT="$TESTPOOL"
log_must zfs $unmountall
unset __ZFS_POOL_RESTRICT

verify_all false

export __ZFS_POOL_RESTRICT="$TESTPOOL"
log_must zfs $mountall
unset __ZFS_POOL_RESTRICT

verify_all true

log_note "Verify that 'zfs $mountcmd' will display " \
	"all ZFS filesystems currently mounted."

verify_mount_display

log_pass "'zfs $mountall' succeeds as root, " \
	"and all available ZFS filesystems are mounted."

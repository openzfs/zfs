#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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

. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_unmount/zfs_unmount.kshlib

#
# DESCRIPTION:
#       Verify that 'zfs unmount -a[f]' succeeds as root.
#
# STRATEGY:
#       1. Create a group of pools with specified vdev.
#       2. Create zfs filesystems within the given pools.
#       3. Mount all the filesystems.
#       4. Verify that 'zfs unmount -a[f]' command succeed,
#	   and all available ZFS filesystems are unmounted.
#	5. Verify that 'zfs mount' is identical with 'df -F zfs'
#

verify_runnable "both"

set -A fs "$TESTFS" "$TESTFS1"
set -A ctr "" "$TESTCTR" "$TESTCTR1" "$TESTCTR/$TESTCTR1"
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

function verify_all
{
	typeset -i i=0
	typeset -i j=0
	typeset path

	while (( i < ${#ctr[*]} )); do

		path=$TESTPOOL
		[[ -n ${ctr[i]} ]] && \
			path=$path/${ctr[i]}

		if is_global_zone ; then
			j=0
			while (( j < ${#vol[*]} )); do
				log_must unmounted "$path/${vol[j]}"
				((j = j + 1))
			done
		fi

		j=0
		while (( j < ${#fs[*]} )); do
			log_must unmounted "$path/${fs[j]}"
			((j = j + 1))
		done

		log_must unmounted "$path"

		((i = i + 1))
	done

	return 0
}


log_assert "Verify that 'zfs $unmountall' succeeds as root, " \
	"and all available ZFS filesystems are unmounted."

log_onexit cleanup_all

log_must setup_all

typeset opt
for opt in "-a" "-fa"; do
	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	log_must zfs $mountall
	unset __ZFS_POOL_RESTRICT

	if [[ $opt == "-fa" ]]; then
		mntpnt=$(get_prop mountpoint ${TESTPOOL}/${TESTCTR}/${TESTFS})
		cd $mntpnt
		log_mustnot zfs unmount -a
	fi

	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	if [[ $opt == "-fa" ]] && is_linux; then
		log_mustnot zfs unmount $opt
		cd /tmp
	fi
	log_must zfs unmount $opt
	unset __ZFS_POOL_RESTRICT

	if [[ $opt == "-fa" ]]; then
		cd  /tmp
	fi

	log_must verify_all
	log_note "Verify that 'zfs $mountcmd' will display " \
	"all ZFS filesystems currently mounted."
	log_must verify_mount_display

done

log_pass "'zfs mount -[f]a' succeeds as root, " \
	"and all available ZFS filesystems are unmounted."

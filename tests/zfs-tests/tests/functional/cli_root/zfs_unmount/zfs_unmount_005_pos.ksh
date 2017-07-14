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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_unmount/zfs_unmount.kshlib

#
# DESCRIPTION:
# If invoke "zfs unmount" with a specific filesystem|mountpoint
# that have been mounted, but it's currently in use,
# it will fail with a return code of 1
# and issue an error message.
# But unmount forcefully will bypass this restriction and
# unmount that given filesystem successfully.
#
# STRATEGY:
# 1. Make sure that the ZFS filesystem is mounted.
# 2. Change directory to that given mountpoint.
# 3. Unmount the file system using the various combinations.
#	- Without force option. (FAILED)
#	- With force option. (PASS)
# 4. Unmount the mountpoint using the various combinations.
#	- Without force option. (FAILED)
#	- With force option. (PASS)
# 5. Verify the above expected results of the filesystem|mountpoint.
#

verify_runnable "both"


set -A cmd "umount" "unmount"
set -A options "" "-f"
set -A dev "$TESTPOOL/$TESTFS" "$TESTDIR"

function do_unmount_multiple #options #expect
{
	typeset opt=$1
	typeset -i expect=${2-0}

	typeset -i i=0
	typeset -i j=0

	while (( i <  ${#cmd[*]} )); do
		j=0
		while (( j < ${#dev[*]} )); do
			mounted ${dev[j]} || \
				log_must zfs $mountcmd ${dev[0]}

			cd $TESTDIR || \
				log_unresolved "Unable change dir to $TESTDIR"

			do_unmount "${cmd[i]}" "$opt" \
				"${dev[j]}" $expect

			cleanup

			((j = j + 1))
		done

		((i = i + 1))
	done
}

log_assert "Verify that 'zfs $unmountcmd <filesystem|mountpoint>' " \
	"with a filesystem which mountpoint is currently in use " \
	"will fail with return code 1, and forcefully will succeeds as root."

log_onexit cleanup

cwd=$PWD

typeset -i i=0

while (( i <  ${#options[*]} )); do
	if [[ ${options[i]} == "-f" ]]; then
		if is_linux; then
			do_unmount_multiple "${options[i]}" 1
		else
			do_unmount_multiple "${options[i]}"
		fi
	else
		do_unmount_multiple "${options[i]}" 1
	fi
        ((i = i + 1))
done

log_pass "'zfs $unmountcmd <filesystem|mountpoint>' " \
	"with a filesystem which mountpoint is currently in use " \
	"will fail with return code 1, and forcefully will succeeds as root."

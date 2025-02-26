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
. $STF_SUITE/tests/functional/cli_root/zfs_unmount/zfs_unmount.kshlib

#
# DESCRIPTION:
# If invoke "zfs unmount [-f]" with a filesystem|mountpoint
# whose mountpoint property is 'legacy' or 'none',
# it will fail with a return code of 1
# and issue an error message.
#
# STRATEGY:
# 1. Make sure that the ZFS filesystem is mounted.
# 2. Apply 'zfs set mountpoint=legacy|none <filesystem>'.
# 3. Unmount the file system using the various combinations.
#	- Without force option. (FAILED)
#	- With force option. (FAILED)
# 4. Unmount the mountpoint using the various combinations.
#	- Without force option. (FAILED)
#	- With force option. (FAILED)
# 5. Verify the above expected results of the filesystem|mountpoint.
#

verify_runnable "both"


set -A cmd "umount" "unmount"
set -A options "" "-f"
set -A dev "$TESTPOOL/$TESTFS" "$TESTDIR"
set -A mopts "legacy" "none"

function do_unmount_multiple #options #expect #mountpoint
{
	typeset opt=$1
	typeset -i expect=${2-0}
	typeset mopt=$3

	typeset -i i=0
	typeset -i j=0

	while (( i <  ${#cmd[*]} )); do
		j=0
		while (( j < ${#dev[*]} )); do
			[[ -n $mopt ]] && \
				log_must zfs set mountpoint=$mopt ${dev[0]}

			do_unmount "${cmd[i]}" "$opt" \
				"${dev[j]}" $expect

			cleanup

			((j = j + 1))
		done

		((i = i + 1))
	done
}

log_assert "Verify that 'zfs $unmountcmd [-f] <filesystem|mountpoint>' " \
	"whose mountpoint property is 'legacy' or 'none' " \
	"will fail with return code 1."

log_onexit cleanup

typeset -i i=0
typeset -i j=0

while (( i < ${#mopts[*]} )); do
	j=0
	while (( j <  ${#options[*]} )); do
		do_unmount_multiple "${options[j]}" 1 "${mopts[i]}"
		((j = j + 1))
	done
	((i = i + 1))
done

log_pass "'zfs $unmountcmd [-f] <filesystem|mountpoint>' " \
	"whose mountpoint property is 'legacy' or 'none' " \
	"will fail with return code 1."

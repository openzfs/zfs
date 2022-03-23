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
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create_common.kshlib

#
# DESCRIPTION:
# 'zfs create -s -V <size> <volume>' can create various-size sparse volume.
#
# STRATEGY:
# 1. Create a volume in the storage pool.
# 2. Verify the volume is created correctly.
# 3. Verify that the volume created has its volsize rounded to the nearest
#    multiple of the blocksize (in this case, the default blocksize)
#

verify_runnable "global"

function cleanup
{
	typeset -i j=0
	while [[ $j -lt ${#size[*]} ]]; do
		destroy_dataset $TESTPOOL/${TESTVOL}${size[j]}
		((j = j + 1))
	done
}

log_onexit cleanup


log_assert "'zfs create -s -V <size> <volume>' succeeds"

typeset -i j=0
while (( $j < ${#size[*]} )); do
	typeset cmdline="zfs create -s -V ${size[j]}  \
			 $TESTPOOL/${TESTVOL}${size[j]}"

	if str=$(eval $cmdline 2>&1); then
		log_note "SUCCESS: $cmdline"
		log_must datasetexists $TESTPOOL/${TESTVOL}${size[j]}
	elif [[ $str == *${VOL_LIMIT_KEYWORD1}* || \
		$str == *${VOL_LIMIT_KEYWORD2}* || \
		$str == *${VOL_LIMIT_KEYWORD3}* ]]
	then
		log_note "UNSUPPORTED: $cmdline"
	else
		log_fail "$cmdline"
	fi

	((j = j + 1))
done

typeset -i j=0
while (( $j < ${#explicit_size_check[*]} )); do
  propertycheck ${TESTPOOL}/${TESTVOL}${explicit_size_check[j]} \
    volsize=${expected_rounded_size[j]} || \
    log_fail "volsize ${size[j]} was not rounded up"

	((j = j + 1))
done

log_pass "'zfs create -s -V <size> <volume>' works as expected."

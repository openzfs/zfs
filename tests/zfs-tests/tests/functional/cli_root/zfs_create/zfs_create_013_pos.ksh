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
. $STF_SUITE/tests/functional/cli_root/zfs_create/zfs_create.cfg

#
# DESCRIPTION:
# 'zfs create -s -V <size> <volume>' can create various-size sparse volume
#  with long fs name
#
# STRATEGY:
# 1. Create a volume in the storage pool.
# 2. Verify the volume is created correctly.
#

verify_runnable "global"

function cleanup
{
	typeset -i j=0
	while [[ $j -lt ${#size[*]} ]]; do
		destroy_dataset $TESTPOOL/${LONGFSNAME}${size[j]}
		((j = j + 1))
	done
}

log_onexit cleanup


log_assert "'zfs create -s -V <size> <volume>' succeeds"

typeset -i j=0
while (( $j < ${#size[*]} )); do
	typeset cmdline="zfs create -s -V ${size[j]} \
			 $TESTPOOL/${LONGFSNAME}${size[j]}"

	if str=$(eval $cmdline 2>&1); then
		log_note "SUCCESS: $cmdline"
		log_must datasetexists $TESTPOOL/${LONGFSNAME}${size[j]}
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

log_pass "'zfs create -s -V <size> <volume>' works as expected."

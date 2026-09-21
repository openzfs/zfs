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

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib

#
# DESCRIPTION:
# Verify 'zfs get all' can deal with invalid scenarios
#
# STRATEGY:
# 1. Define invalid scenarios for 'zfs get all'
# 2. Run zfs get with those invalid scenarios
# 3. Verify that zfs get fails with invalid scenarios
#

verify_runnable "both"

log_assert "Verify 'zfs get all' fails with invalid combination scenarios."

set -f	# Force ksh ignore '?' and '*'
#
# Combinations such as "all -r" are deliberately absent: they are rejected
# only when the C library declines to reorder arguments.  Both glibc and
# musl getopt_long(3) permute by default, so on Linux those forms are
# accepted and must not be asserted to fail here.
#
set -A  bad_combine "ALL" "\-R all" "-P all" "-h all" "-rph all" "-RpH all" "-PrH all" \
		"-o all" "-s all" "-s none=getsubopt" "-t filesystem=getsubopt" \
		"-? all" "-* all" "-?* all" "all -ph" \
		"all -rph $TESTPOOL" "all,available,reservation $TESTPOOL" \
		"all $TESTPOOL?" "all $TESTPOOL*" "all nonexistpool"

typeset -i i=0
while (( i < ${#bad_combine[*]} ))
do
	log_mustnot eval "zfs get ${bad_combine[i]} >/dev/null"

	(( i = i + 1 ))
done

log_pass "'zfs get all' fails with invalid combinations scenarios as expected."

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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
# A negative depth or a non numeric depth should fail in 'zfs list -d <n>'
#
# STRATEGY:
# 1. Run zfs list -d with negative depth or non numeric depth
# 2. Verify that zfs list returns error
#

verify_runnable "both"

log_assert "A negative depth or a non numeric depth should fail in 'zfs list -d <n>'"

set -A  badargs "a" "AB" "aBc" "2A" "a2b" "aB2" "-1" "-32" "-999"

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "zfs list -d ${badargs[i]} $DEPTH_FS >/dev/null 2>&1"
	(( i = i + 1 ))
done

log_pass "A negative depth or a non numeric depth should fail in 'zfs list -d <n>'"

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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting invalid primarycache and secondarycache on file system or volume.
# It should fail.
#
# STRATEGY:
# 1. Create pool, then create filesystem & volume within it.
# 2. Setting invalid {primary|secondary}cache value, it should fail.
#

verify_runnable "both"

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values  "12345" "null" "not_existed" "abcd1234"

log_assert "Setting invalid {primary|secondary}cache on fs and volume, " \
	"It should fail."

typeset -i i=0
typeset -i j=0
for propname in "primarycache" "secondarycache"
do
	while (( i < ${#dataset[@]} )); do
		j=0
		while (( j < ${#values[@]} )); do
			log_mustnot zfs set $propname=${values[j]} ${dataset[i]}
			(( j += 1 ))
		done
		(( i += 1 ))
	done
done

log_pass "Setting invalid {primary|secondary}cache on fs or volume fail as expected."

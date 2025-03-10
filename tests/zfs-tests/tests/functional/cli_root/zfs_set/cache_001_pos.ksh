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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting a valid primarycache and secondarycache on file system or volume.
# It should be successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem & volume within it.
# 2. Setting valid cache value, it should be successful.
#

verify_runnable "both"

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values  "none" "all" "metadata"

log_assert "Setting a valid {primary|secondary}cache on file system and volume, " \
	"It should be successful."

typeset -i i=0
typeset -i j=0
for propname in "primarycache" "secondarycache"
do
	while (( i < ${#dataset[@]} )); do
		j=0
		while (( j < ${#values[@]} )); do
			set_n_check_prop "${values[j]}" "$propname" "${dataset[i]}"
			(( j += 1 ))
		done
		(( i += 1 ))
	done
done

log_pass "Setting a valid {primary|secondary}cache on file system or volume pass."

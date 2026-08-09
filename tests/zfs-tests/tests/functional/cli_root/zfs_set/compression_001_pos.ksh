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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting a valid compression on file system or volume.
# It should be successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem & volume within it.
# 2. Setting valid value, it should be successful.
#

verify_runnable "both"

set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"
set -A values "${compress_prop_vals[@]}"

log_assert "Setting a valid compression on file system and volume, " \
	"It should be successful."

typeset -i i=0
typeset -i j=0
for propname in "compression" "compress"
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

log_pass "Setting a valid compression on file system or volume pass."

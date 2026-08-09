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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting invalid value to mountpoint, checksum, atime, readonly, setuid,
# zoned, recordsize, or canmount on a file system, volume. It should be failed.
#
# STRATEGY:
# 1. Create pool, then create file system & volume within it.
# 2. Setting invalid value, it should be failed.
#

verify_runnable "both"

set -A props "" "mountpoint" "checksum" "compression" "atime" "readonly" \
	"setuid" "canmount"
if is_freebsd; then
	props+=("jailed")
else
	props+=("zoned")
fi

set -A values "" "mountpoint" "checksum" "compression" "atime" "readonly" \
	"setuid" "zoned" "0" "-?" "-on" "--on" "*" "?" "Legacy" "NONE" "oN" \
	"On" "ON" "ofF" "OFf" "oFF" "Off" "OfF" "OFF" "LzJb" "lZJb" "LZjB" \
	"blad" "default" "TESTPOOL" "$TESTPOOL/$TESTCTR" \
	"$TESTPOOL/$TESTCTR/$TESTFS" "$TESTPOOL/$TESTFS"
set -A dataset "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTVOL"

log_assert "Setting invalid value to mountpoint, checksum, compression, atime,"\
	"readonly, setuid, zoned or canmount on a file system file system or volume." \
	"It should be failed."

typeset -i i=0
typeset -i j=0
typeset -i k=0
while (( i < ${#dataset[@]} )); do
	j=0
	while (( j < ${#props[@]} )); do
		k=0
		while (( k < ${#values[@]} )); do
			set_n_check_prop "${values[k]}" "${props[j]}" \
				"${dataset[i]}" false
			(( k += 1 ))
		done
		(( j += 1 ))
	done
	# Additional recordsize
	set_n_check_prop "32768K" "recordsize" "${dataset[i]}" false
	set_n_check_prop "128B" "recordsize" "${dataset[i]}" false
	(( i += 1 ))
done

log_pass "Setting invalid value to mountpoint, checksum, compression, atime, " \
	"readonly, setuid, zoned or canmount on file system or volume pass."

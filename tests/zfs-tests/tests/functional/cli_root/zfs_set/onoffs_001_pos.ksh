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
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting a valid value to atime, readonly, setuid or zoned on file
# system or volume. It should be successful.
#
# STRATEGY:
# 1. Create pool and filesystem & volume within it.
# 2. Setting valid value, it should be successful.
#

verify_runnable "both"

function cleanup
{
	log_must zfs mount -a
}

log_onexit cleanup

set -A props "atime" "readonly" "setuid"
if is_freebsd; then
	props+=("jailed")
else
	props+=("zoned")
fi
set -A values "on" "off"

if is_global_zone ; then
	set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTVOL"
else
	set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR"
fi

log_assert "Setting a valid value to atime, readonly, setuid or zoned on file" \
	"system or volume. It should be successful."

typeset -i i=0
typeset -i j=0
typeset -i k=0
while (( i < ${#dataset[@]} )); do
	j=0
	while (( j < ${#props[@]} )); do
		k=0
		while (( k < ${#values[@]} )); do
			if [[ ${dataset[i]} == "$TESTPOOL/$TESTVOL" &&  \
			    ${props[j]} != "readonly" ]]
			then
				set_n_check_prop "${values[k]}" "${props[j]}" \
				    "${dataset[i]}" "false"
			elif [[ ${props[j]} == "zoned" ]] ; then
				if is_global_zone ; then
					set_n_check_prop \
					    "${values[k]}" "${props[j]}" \
					    "${dataset[i]}"
				else
					set_n_check_prop \
					    "${values[k]}" "${props[j]}" \
					    "${dataset[i]}" "false"
				fi

			else
				set_n_check_prop "${values[k]}" "${props[j]}" \
					"${dataset[i]}"
			fi

			(( k += 1 ))
		done
		(( j += 1 ))
	done
	(( i += 1 ))
done

log_pass "Setting a valid value to atime, readonly, setuid or zoned on file" \
	"system or volume pass."

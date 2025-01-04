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

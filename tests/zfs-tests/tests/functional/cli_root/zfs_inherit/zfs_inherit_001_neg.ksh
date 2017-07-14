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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs inherit' should return an error when attempting to inherit
# properties which are not inheritable.
#
# STRATEGY:
# 1. Create an array of properties which cannot be inherited
# 2. For each property in the array, execute 'zfs inherit'
# 3. Verify an error is returned.
#

verify_runnable "both"

# Define uninherited properties and their short name.
typeset props_str="type used available avail creation referenced refer \
		compressratio ratio mounted origin quota reservation \
		reserv volsize volblocksize volblock"

zfs upgrade -v > /dev/null 2>&1
if [[ $? -eq 0 ]]; then
	props_str="$props_str version"
fi

set -A prop $props_str canmount


log_assert "'zfs inherit' should return an error when attempting to inherit" \
	" un-inheritable properties."

typeset -i i=0
for obj in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
	i=0
	while [[ $i -lt ${#prop[*]} ]]; do
		orig_val=$(get_prop ${prop[i]} $obj)

		log_mustnot zfs inherit ${prop[i]} $obj

		new_val=$(get_prop ${prop[i]} $obj)

		if [[ $new_val != $orig_val ]]; then
			log_fail "${prop[i]} property changed from $orig_val "
				" to $new_val"
		fi
		((i = i + 1))
	done
done

log_pass "'zfs inherit' failed as expected when attempting to inherit" \
	" un-inheritable properties."

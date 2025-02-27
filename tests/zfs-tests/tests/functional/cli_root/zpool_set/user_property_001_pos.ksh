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
# Copyright (c) 2023 by Klara Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_set/zpool_set_common.kshlib

#
# DESCRIPTION:
#	ZFS can set any valid user-defined pool property.
#
# STRATEGY:
#	1. Combine all kind of valid characters into a valid user-defined
#	   property name.
#	2. Random get a string as the value.
#	3. Verify all the valid user-defined pool properties can be set to a
#	   pool.
#

verify_runnable "both"

log_assert "ZFS can set any valid user-defined pool property."
log_onexit cleanup_user_prop $TESTPOOL

typeset -a names=()
typeset -a values=()

# Longest property name (255 bytes, which is the 256-byte limit minus 1 byte
# for the null byte)
names+=("$(awk 'BEGIN { printf "x:"; while (c++ < (256 - 2 - 1)) printf "a" }')")
values+=("long-property-name")
# Longest property value (8191 bytes, which is the 8192-byte limit minus 1 byte
# for the null byte).
names+=("long:property:value")
values+=("$(awk 'BEGIN { while (c++ < (8192 - 1)) printf "A" }')")
# Valid property names
for i in {1..10}; do
	typeset -i len
	((len = RANDOM % 32))
	names+=("$(valid_user_property $len)")
	((len = RANDOM % 512))
	values+=("$(user_property_value $len)")
done

typeset -i i=0
while ((i < ${#names[@]})); do
	typeset name="${names[$i]}"
	typeset value="${values[$i]}"

	log_must zpool set "$name=$value" "$TESTPOOL"
	log_must check_user_prop "$TESTPOOL" "$name" "$value"

	((i += 1))
done

log_pass "ZFS can set any valid user-defined pool property passed."

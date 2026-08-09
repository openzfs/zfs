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

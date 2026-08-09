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
#	ZFS can handle any invalid user-defined pool property.
#
# STRATEGY:
#	1. Combine all kind of invalid user pool property names.
#	2. Random get a string as the value.
#	3. Verify all the invalid user-defined pool properties can not be set
#	   to the pool.
#

verify_runnable "both"

log_assert "ZFS can handle any invalid user pool property."
log_onexit cleanup_user_prop $TESTPOOL

typeset -a names=()
typeset -a values=()

# A property name that is too long consists of 256 or more bytes (which is (1)
# the 256-byte limit (2) minus 1 byte for the null byte (3) plus 1 byte to
# reach back over the limit).
names+=("$(awk '
	BEGIN {
		# Print a 2-byte prefix of the name.
		printf "x:";
		# Print the remaining 254 bytes.
		while (c++ < (256 - 2 - 1 + 1))
			printf "a"
	}'
)")
values+=("too-long-property-name")
# A property value that is too long consists of at least 8192 bytes.
# The smallest too-long value is (1) the limit (2) minus 1 byte for the null
# byte (2) plus 1 byte to reach back over the limit).
names+=("too:long:property:value")
values+=("$(awk 'BEGIN { while (c++ < (8192 - 1 + 1)) printf "A" }')")
# Invalid property names
for i in {1..10}; do
	typeset -i len
	((len = RANDOM % 32))
	names+=("$(invalid_user_property $len)")
	((len = RANDOM % 512))
	values+=("$(user_property_value $len)")
done

typeset -i i=0
while ((i < ${#names[@]})); do
	typeset name="${names[$i]}"
	typeset value="${values[$i]}"

	log_mustnot zpool set $name=$value $TESTPOOL
	log_mustnot check_user_prop $TESTPOOL \"$name\" \"$value\"

	((i += 1))
done

log_pass "ZFS can handle invalid user pool property passed."

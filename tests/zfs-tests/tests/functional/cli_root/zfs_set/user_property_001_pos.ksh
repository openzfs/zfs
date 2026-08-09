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

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
#	ZFS can set any valid user defined property to the non-readonly dataset.
#
# STRATEGY:
#	1. Loop pool, fs and volume.
#	2. Combine all kind of valid characters into a valid user defined
#	   property name.
#	3. Random get a string as the value.
#	4. Verify all the valid user defined properties can be set to the
#	   dataset in #1.
#

verify_runnable "both"

log_assert "ZFS can set any valid user defined property to the non-readonly " \
	"dataset."
log_onexit cleanup_user_prop $TESTPOOL

typeset -a names=()
typeset -a values=()

# Longest property name (255 bytes)
names+=("$(awk 'BEGIN { printf "x:"; while (c++ < 253) printf "a" }')")
values+=("too-long-property-name")
# Longest property value (8191 bytes)
names+=("too:long:property:value")
values+=("$(awk 'BEGIN { while (c++ < 8191) printf "A" }')")
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

	for dtst in $TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL; do
		log_must eval "zfs set $name='$value' $dtst"
		log_must eval "check_user_prop $dtst $name '$value'"
	done

	((i += 1))
done

log_pass "ZFS can set any valid user defined property passed."

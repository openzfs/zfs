#!/bin/ksh -p
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

# Too long property name (256 bytes, which is the 256-byte limit minus 1 byte
# for the null byte plus 1 byte to reach back over the limit)
names+=("$(awk 'BEGIN { printf "x:"; while (c++ < (256 - 2 - 1 + 1)) printf "a" }')")
values+=("too-long-property-name")
# Too long property value (the limits are 1024 on FreeBSD and 4096 on Linux, so
# pick the right one; the too long value is, e.g., the limit minus 1 bytes for the
# null byte plus 1 byte to reach back over the limit)
if is_linux; then
	typeset ZFS_MAXPROPLEN=4096
else
	typeset ZFS_MAXPROPLEN=1024
fi
names+=("too:long:property:value")
values+=("$(awk -v max="$ZFS_MAXPROPLEN" 'BEGIN { while (c++ < (max - 1 + 1)) printf "A" }')")
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

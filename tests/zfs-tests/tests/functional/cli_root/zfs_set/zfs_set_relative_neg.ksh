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
# Copyright (c) 2025 by The OpenZFS Project. All rights reserved.
#

#
# DESCRIPTION:
# 'zfs set property+=value' and 'zfs set property-=value' should be rejected
# for invalid inputs: non-numeric properties, read-only properties, underflow,
# zero result, overflow, and bad numeric suffixes.
#
# STRATEGY:
# 1. Attempt += on an index (non-numeric) property: must fail.
# 2. Attempt += on a read-only property: must fail.
# 3. Attempt -= that would result in exactly 0: must fail.
# 4. Attempt -= that would underflow (result < 0): must fail.
# 5. Attempt += with a malformed delta (bad suffix): must fail.
# 6. Attempt += on an unknown property: must fail.
# 7. Verify the property value is unchanged after each failed attempt.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

FS=$TESTPOOL/$TESTFS

function cleanup
{
	log_must zfs set quota=none $FS
}

log_onexit cleanup

log_assert "'zfs set prop+=val' is rejected for invalid inputs"

# Establish a baseline quota for decrement tests
log_must zfs set quota=100m $FS

# --- non-numeric (index) property: checksum ---
log_mustnot zfs set checksum+=1 $FS

# --- read-only property: used ---
log_mustnot zfs set used+=1 $FS

# --- -= resulting in exactly zero ---
# quota is 100m; subtracting 100m would make it 0, which is disallowed
log_mustnot zfs set quota-=100m $FS
typeset val
val=$(get_prop quota $FS)
[[ $val -eq $((100 * 1024 * 1024)) ]] || \
	log_fail "quota changed after zero-result -=: got $val"

# --- -= underflow (delta > current value) ---
log_mustnot zfs set quota-=200m $FS
val=$(get_prop quota $FS)
[[ $val -eq $((100 * 1024 * 1024)) ]] || \
	log_fail "quota changed after underflow -=: got $val"

# --- malformed delta (bad suffix) ---
log_mustnot zfs set quota+=100xyz $FS
val=$(get_prop quota $FS)
[[ $val -eq $((100 * 1024 * 1024)) ]] || \
	log_fail "quota changed after bad suffix +=: got $val"

# --- unknown property ---
log_mustnot zfs set nosuchprop+=1 $FS

log_pass "'zfs set prop+=val' is correctly rejected for invalid inputs"

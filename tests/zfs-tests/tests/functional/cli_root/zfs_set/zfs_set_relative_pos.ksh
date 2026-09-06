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
# 'zfs set property+=value' and 'zfs set property-=value' (relative
# increment/decrement) should succeed for writable numeric properties.
#
# STRATEGY:
# 1. Set a numeric property (quota) to a known value.
# 2. Use += to increment it and verify the new value.
# 3. Use -= to decrement it and verify the new value.
# 4. Use += when the property is unset (none/0) and verify it becomes the delta.
# 5. Apply += to multiple datasets in one invocation and verify each.
# 6. Verify reservation+= behaves correctly.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

FS=$TESTPOOL/$TESTFS

function cleanup
{
	log_must zfs set quota=none $FS
	log_must zfs set reservation=none $FS
}

log_onexit cleanup

log_assert "'zfs set prop+=val' and 'zfs set prop-=val' work for numeric props"

# --- quota += ---
log_must zfs set quota=100m $FS
log_must zfs set quota+=50m $FS
typeset val
val=$(get_prop quota $FS)
# 150 MiB = 157286400 bytes
[[ $val -eq $((150 * 1024 * 1024)) ]] || log_fail "quota+= expected 157286400, got $val"

# --- quota -= ---
log_must zfs set quota-=50m $FS
val=$(get_prop quota $FS)
# 100 MiB = 104857600 bytes
[[ $val -eq $((100 * 1024 * 1024)) ]] || log_fail "quota-= expected 104857600, got $val"

# --- += when quota is none (treated as 0) ---
log_must zfs set quota=none $FS
log_must zfs set quota+=200m $FS
val=$(get_prop quota $FS)
[[ $val -eq $((200 * 1024 * 1024)) ]] || log_fail "quota+= from none expected 209715200, got $val"

# --- reservation+= ---
log_must zfs set reservation=50m $FS
log_must zfs set reservation+=10m $FS
val=$(get_prop reservation $FS)
[[ $val -eq $((60 * 1024 * 1024)) ]] || log_fail "reservation+= expected 62914560, got $val"
log_must zfs set reservation=none $FS

# --- multi-dataset: += applied to two filesystems ---
typeset FS2=$TESTPOOL/${TESTFS}_rel2
log_must zfs create $FS2
log_must zfs set quota=100m $FS $FS2
log_must zfs set quota+=20m $FS $FS2
typeset v1 v2
v1=$(get_prop quota $FS)
v2=$(get_prop quota $FS2)
[[ $v1 -eq $((120 * 1024 * 1024)) ]] || log_fail "multi-dataset: $FS quota wrong: $v1"
[[ $v2 -eq $((120 * 1024 * 1024)) ]] || log_fail "multi-dataset: $FS2 quota wrong: $v2"
log_must zfs set quota=none $FS $FS2
log_must zfs destroy $FS2

log_pass "'zfs set prop+=val' and 'zfs set prop-=val' succeed for numeric props"

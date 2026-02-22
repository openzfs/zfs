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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# AnyRAID mirror4 can survive having 1-4 failed disks.
#
# STRATEGY:
# 1. Write several files to the ZFS filesystem mirror.
# 2. Override the selected disks of the mirror with zeroes.
# 4. Verify that all the file contents are unchanged on the file system.
#

verify_runnable "global"

log_assert "AnyRAID mirror4 can survive having 1-4 failed disks"

log_must create_sparse_files "disk" 5 $DEVSIZE

clean_mirror_spec_cases "anymirror4 $disk0 $disk1 $disk2 $disk3 $disk4" \
	"$disk0" \
	"$disk4" \
	"\"$disk0 $disk1\"" \
	"\"$disk1 $disk4\"" \
	"\"$disk0 $disk1 $disk3\"" \
	"\"$disk2 $disk3 $disk4\"" \
	"\"$disk0 $disk1 $disk2 $disk3\"" \
	"\"$disk0 $disk2 $disk3 $disk4\""

log_pass "AnyRAID mirror4 can survive having 1-4 failed disks"

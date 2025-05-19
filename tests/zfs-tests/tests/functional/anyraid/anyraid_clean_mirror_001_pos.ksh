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
# AnyRAID mirror1 can survive having 1 failed disk.
#
# STRATEGY:
# 1. Write several files to the ZFS filesystem mirror.
# 2. Override one of the disks of the mirror with zeroes.
# 3. Verify that all the file contents are unchanged on the file system.
#

verify_runnable "global"

log_assert "AnyRAID mirror1 can survive having 1 failed disk"

log_must create_sparse_files "disk" 3 $DEVSIZE

spec_cases "anyraid1 $disk0 $disk1" \
	"$disk0" \
	"$disk1"

spec_cases "anyraid1 $disk0 $disk1 $disk2" \
	"$disk0" \
	"$disk1" \
	"$disk2"

log_pass "AnyRAID mirror1 can survive having 1 failed disk"

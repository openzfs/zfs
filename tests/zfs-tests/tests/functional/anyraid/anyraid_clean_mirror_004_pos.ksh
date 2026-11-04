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

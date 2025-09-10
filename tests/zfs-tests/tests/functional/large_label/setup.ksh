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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2025 by Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/large_label/large_label.kshlib

[[ ! -d $TESTDIR1 ]] && log_must mkdir -p $TESTDIR1
log_must create_pool $BASE_POOL $DISKS
log_must zfs set mountpoint=$TESTDIR1 $BASE_POOL

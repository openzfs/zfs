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
# Copyright (c) 2023 by Proxmox. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

# DESCRIPTION:
# Verify that nfs shares persist after zfs mount -a
#
# STRATEGY:
# 1. Verify that the filesystem is not shared.
# 2. Enable the 'sharenfs' property
# 3. Verify filesystem is shared
# 4. Invoke 'zfs mount -a'
# 5. Verify filesystem is still shared

verify_runnable "global"

function cleanup
{
	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	is_shared $TESTPOOL/$TESTFS && \
		log_must unshare_fs $TESTPOOL/$TESTFS
	log_must zfs share -a
}


log_onexit cleanup

cleanup

log_must zfs set sharenfs="on" $TESTPOOL/$TESTFS
log_must is_shared $TESTPOOL/$TESTFS
log_must is_exported $TESTPOOL/$TESTFS

log_must zfs mount -a
log_must is_shared $TESTPOOL/$TESTFS
log_must is_exported $TESTPOOL/$TESTFS

log_pass "Verify that nfs shares persist after zfs mount -a"

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

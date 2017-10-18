#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2013 by Delphix. All rights reserved.
# Copyright (c) 2016, Intel Corporation.
#

. $STF_SUITE/tests/functional/special/special.kshlib

#
# DESCRIPTION:
#	Using zpool split command to detach disks from mirrored special pool
#	to create a new pool with the detached disks - should be disabled.
#

verify_runnable "global"

log_assert "Zpool split command successfully fails."
log_onexit cleanup


log_must zpool create $TESTPOOL mirror $ZPOOL_DISKS \
		 special mirror $MD_DISKS
log_mustnot zpool split $TESTPOOL split_pool $MD_DISK1
log_must zpool destroy -f $TESTPOOL

log_pass "Zpool split command successfully fails."

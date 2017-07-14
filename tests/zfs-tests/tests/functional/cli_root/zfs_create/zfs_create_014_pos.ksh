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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# check 'zfs create <filesystem>' works at the name length boundary
#
# STRATEGY:
# 1. Verify creating filesystem with name length 255 would succeed
# 2. Verify creating filesystem with name length 256 would fail
# 3. Verify the pool can be re-imported

verify_runnable "both"

# namelen 255 and 256
TESTFS1=$(for i in $(seq $((254 - ${#TESTPOOL}))); do echo z ; done | tr -d '\n')
TESTFS2=$(for i in $(seq $((255 - ${#TESTPOOL}))); do echo z ; done | tr -d '\n')

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 &&
		log_must zfs destroy $TESTPOOL/$TESTFS1
}

log_onexit cleanup

log_assert "'zfs create <filesystem>' can create a ZFS filesystem with name length 255."

log_must zfs create $TESTPOOL/$TESTFS1
log_mustnot zfs create $TESTPOOL/$TESTFS2
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_pass "'zfs create <filesystem>' works as expected."

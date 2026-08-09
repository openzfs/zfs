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
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1
}

log_onexit cleanup

log_assert "'zfs create <filesystem>' can create a ZFS filesystem with name length 255."

log_must zfs create $TESTPOOL/$TESTFS1
log_mustnot zfs create $TESTPOOL/$TESTFS2
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_pass "'zfs create <filesystem>' works as expected."

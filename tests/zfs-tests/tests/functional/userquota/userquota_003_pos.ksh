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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the basic function of set/get userquota and groupquota on fs
#
#
# STRATEGY:
#       1. Set userquota on fs and check the zfs get
#       2. Set groupquota on fs and check the zfs get
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "Check the basic function of set/get userquota and groupquota on fs"

log_note "Check the set|get userquota@$QUSER1 and groupquota@QGROUP"
log_must zfs set userquota@$QUSER1=$UQUOTA_SIZE $QFS
log_must check_quota "userquota@$QUSER1" $QFS "$UQUOTA_SIZE"

log_must zfs set groupquota@$QGROUP=$GQUOTA_SIZE $QFS
log_must check_quota "groupquota@$QGROUP" $QFS "$GQUOTA_SIZE"

log_pass "Check the basic function of set/get userquota on fs passed as expect"

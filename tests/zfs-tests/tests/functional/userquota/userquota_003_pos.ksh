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

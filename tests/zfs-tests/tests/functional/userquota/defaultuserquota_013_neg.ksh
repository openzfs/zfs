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
#
# DESCRIPTION:
#       Check that defaultuserquota is applicable to individual users
#
#
# STRATEGY:
#       1. Set defaultuserquota
#       2. Check that user1 is able to write up to defaultuserquota size
#       2. Check that request fails when user1 exceed2 defaultuserquota size
#       2. Check that user2 is able to write up to defaultuserquota size
#       2. Check that request fails when user2 exceed2 defaultuserquota size
#
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "If defaultuserquota is not applicable to individual users, it will fail"

log_note "Check the defaultuserquota"
log_must zfs set defaultuserquota=$UQUOTA_SIZE $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $UQUOTA_SIZE $QFILE
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE
log_must user_run $QUSER2 mkfile $UQUOTA_SIZE $QFILE2
sync_pool
log_mustnot user_run $QUSER2 mkfile 1 $OFILE2
sync_pool
cleanup_quota

log_pass "defaultuserquota is not applicable to individual users, failed as expected"

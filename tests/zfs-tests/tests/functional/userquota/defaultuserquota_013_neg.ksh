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

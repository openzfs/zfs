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
#       Check that defaultquotas can override the per user/group quota
#
#
# STRATEGY:
#       1. Set defaultuserquota and userquota (double then defaultuserquota)
#       2. The write operation should not fail when defaultuserquota is exceed
#       3. The write operation should fail when userquota is exceed
#       4. Set defaultgroupquota and group (double then defaultgroupquota)
#       5. The write operation should not fail when defaultgroupquota is exceed
#       6. The write operation should fail when group quota is exceed
#
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "If per user/group quota is able to override defaultquota, it will fail"

log_note "Check the defaultuserquota"
log_must zfs set defaultuserquota=$UQUOTA_SIZE $QFS
log_must zfs set userquota@$QUSER1=$(($UQUOTA_SIZE * 2 + 1)) $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $UQUOTA_SIZE $QFILE
sync_pool
log_must user_run $QUSER1 mkfile 1 $OFILE
sync_pool
log_must user_run $QUSER1 mkfile $UQUOTA_SIZE $QFILE2
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE2
cleanup_quota
zfs set defaultuserquota=none $QFS

log_note "Check the defaultgroupquota"
log_must zfs set defaultgroupquota=$GQUOTA_SIZE $QFS
log_must zfs set groupquota@$QGROUP=$(($GQUOTA_SIZE * 2 + 1)) $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $GQUOTA_SIZE $QFILE
sync_pool
log_must user_run $QUSER1 mkfile 1 $OFILE
sync_pool
log_must user_run $QUSER1 mkfile $GQUOTA_SIZE $QFILE2
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE2
cleanup_quota
zfs set defaultuserquota=none tank

log_pass "Per User/Group quota override default quota, failed as expected"

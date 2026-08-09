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

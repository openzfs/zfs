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
# Copyright (c) 2016 by Jinshan Xiong. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
#
# DESCRIPTION:
#       Check the basic function of the userobjquota and groupobjquota
#
#
# STRATEGY:
#       1. Set userobjquota and overwrite the quota size
#       2. Creating new object should fail with Disc quota exceeded
#       3. Set groupobjquota and overwrite the quota size
#       4. Creating new object should fail with Disc quota exceeded
#
#

function cleanup
{
	log_must rm -f ${QFILE}_*
	cleanup_quota
}

log_onexit cleanup

log_assert "If creating object exceeds {user|group}objquota count, it will fail"

mkmount_writable $QFS
log_must zfs set xattr=sa $QFS

log_note "Check the userobjquota@$QUSER1"
log_must zfs set userobjquota@$QUSER1=100 $QFS
log_must user_run $QUSER1 mkfiles ${QFILE}_1 100
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE
cleanup_quota

log_note "Check the groupobjquota@$QGROUP"
log_must zfs set groupobjquota@$QGROUP=200 $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfiles ${QFILE}_2 100
sync_pool
log_mustnot user_run $QUSER2 mkfile 1 $OFILE

cleanup
log_pass "Creating objects exceeds {user|group}objquota count, it as expect"

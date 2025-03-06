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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Verify option '-c' will remove the created permission set.
#
# STRATEGY:
#	1. Set created time set to $ROOT_TESTFS.
#	2. Allow permission create to $STAFF1 on $ROOT_TESTFS.
#	3. Create $SUBFS and verify $STAFF1 have created time permissions.
#	4. Verify $STAFF1 has created time permission.
#	5. Unallow created time permission with option '-c'.
#	6. Created $SUBFS and verify $STAFF1 have not created time permissions.
#

verify_runnable "both"

log_assert "Verify option '-c' will remove the created permission set."
log_onexit restore_root_datasets

log_must zfs allow -c $LOCAL_SET $ROOT_TESTFS
log_must zfs allow -l $STAFF1 create,mount $ROOT_TESTFS

# Create $SUBFS and verify $SUBFS has created time permissions.
user_run $STAFF1 zfs create $SUBFS
if ! datasetexists $SUBFS ; then
	log_fail "ERROR: ($STAFF1): zfs create $SUBFS"
fi
log_must verify_perm $SUBFS $LOCAL_SET $STAFF1

#
# After unallow -c, create $SUBFS2 and verify $SUBFS2 has not created time
# permissions any more.
#
log_must zfs unallow -c $LOCAL_SET $ROOT_TESTFS
user_run $STAFF1 zfs create $SUBFS2
if ! datasetexists $SUBFS2 ; then
	log_fail "ERROR: ($STAFF1): zfs create $SUBFS2"
fi
log_must verify_noperm $SUBFS2 $LOCAL_SET $STAFF1

log_pass "Verify option '-c' will remove the created permission set passed."

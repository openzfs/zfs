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

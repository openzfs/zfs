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
#	zfs unallow will not remove those permissions which inherited from
#	its parent filesystem.
#
# STRATEGY:
#	1. Assign perm1 to $ROOT_TESTFS
#	2. Create $SUBFS and assign perm2 to it.
#	3. Verify unallow can not affect perm1 on $SUBFS
#

verify_runnable "both"

log_assert "zfs unallow won't remove those permissions which inherited from " \
	"its parent dataset."
log_onexit restore_root_datasets

perm1="atime,devices"; perm2="compression,checksum"
log_must zfs create $SUBFS
log_must zfs allow $STAFF1 $perm1 $ROOT_TESTFS
log_must zfs allow $STAFF1 $perm2 $SUBFS

log_must verify_perm $SUBFS ${perm1},${perm2} $STAFF1
#
# Athrough unallow the permissions which don't exists on the specific dataset
# return 0, the inherited permissions can't be removed in fact.
#
log_must zfs unallow -u $STAFF1 $perm1 $SUBFS
log_must verify_perm $SUBFS ${perm1},${perm2} $STAFF1

log_pass "Verify zfs unallow won't remove inherited permissions passed."

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
# Portions Copyright 2020 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify chown works with POSIX ACLs.
#	Regression test for https://github.com/openzfs/zfs/issues/10043
#
# STRATEGY:
#	1. Prepare an appropriate ACL on the test directory
#	2. Change the owner of the directory
#	3. Reset and set the ACLs for test directory owned by the user
#

verify_runnable "both"
log_assert "Verify chown works with POSIX ACLs"
log_onexit cleanup

log_must setfacl -d -m u:$ZFS_ACL_STAFF1:rwx $TESTDIR
log_must setfacl -b $TESTDIR

log_must chown $ZFS_ACL_STAFF1 $TESTDIR
log_must setfacl -b $TESTDIR
log_must setfacl -d -m u:$ZFS_ACL_STAFF1:rwx $TESTDIR
log_must chown 0 $TESTDIR

log_pass "chown works with POSIX ACLs"

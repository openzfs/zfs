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

log_must setfacl -d -m u:$ZFS_ACL_STAFF1:rwx $TESTDIR
log_must setfacl -b $TESTDIR

log_must chown $ZFS_ACL_STAFF1 $TESTDIR
log_must setfacl -b $TESTDIR
log_must setfacl -d -m u:$ZFS_ACL_STAFF1:rwx $TESTDIR
log_must chown 0 $TESTDIR

log_pass "chown works with POSIX ACLs"

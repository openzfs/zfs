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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that ACLs survive remount.
#
# STRATEGY:
#	1. Test presence of default and regular ACLs after remount
#	   a. Can set and list ACL before remount
#	   b. Can list ACL after remount
#

verify_runnable "both"
log_assert "Verify regular and default POSIX ACLs survive  remount"

typeset acl_str1="^group:$ZFS_ACL_STAFF_GROUP:-wx$"
typeset acl_str2="^default:group:$ZFS_ACL_STAFF_GROUP:-wx$"
typeset ACLDIR="$TESTDIR/dir.1"

log_note "Testing access to DIRECTORY"
log_must mkdir $ACLDIR
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:wx $ACLDIR
log_must setfacl -d -m g:$ZFS_ACL_STAFF_GROUP:wx $ACLDIR

if getfacl $ACLDIR 2> /dev/null | grep -q "$acl_str1" &&
	getfacl $ACLDIR 2> /dev/null | grep -q "$acl_str2"
then
	log_must zfs unmount $TESTPOOL/$TESTFS
	log_must zfs mount $TESTPOOL/$TESTFS
	log_must eval "getfacl $ACLDIR 2> /dev/null | grep -q \"$acl_str1\""
	log_must eval "getfacl $ACLDIR 2> /dev/null | grep -q \"$acl_str2\""
	log_pass "POSIX ACLs survive remount"
else
	log_fail "Group '$ZFS_ACL_STAFF_GROUP' does not have 'rwx'"
fi

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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that ACLs survive remount.
#	Regression test for https://github.com/zfsonlinux/zfs/issues/4520
#
# STRATEGY:
#	1. Test presence of default and regular ACLs after remount
#	   a. Can set and list ACL before remount
#	   b. Can list ACL after remount
#

verify_runnable "both"
log_assert "Verify regular and default POSIX ACLs survive  remount"

typeset output=/tmp/zfs-posixacl.$$
typeset acl_str1="^group:${ZFS_ACL_STAFF_GROUP}:-wx$"
typeset acl_str2="^default:group:${ZFS_ACL_STAFF_GROUP}:-wx$"
typeset ACLDIR="${TESTDIR}/dir.1"

log_note "Testing access to DIRECTORY"
log_must $MKDIR $ACLDIR
log_must $SETFACL -m g:${ZFS_ACL_STAFF_GROUP}:wx $ACLDIR
log_must $SETFACL -d -m g:${ZFS_ACL_STAFF_GROUP}:wx $ACLDIR
$GETFACL $ACLDIR 2> /dev/null | $EGREP -q "${acl_str1}"
if [ "$?" -eq "0" ]; then
	$GETFACL $ACLDIR 2> /dev/null | $EGREP -q "${acl_str2}"
fi

if [ "$?" -eq "0" ]; then
	log_must $ZFS unmount $TESTPOOL/$TESTFS
	log_must $ZFS mount $TESTPOOL/$TESTFS
	log_must eval '$GETFACL $ACLDIR 2> /dev/null | $EGREP -q "${acl_str1}"'
	log_must eval '$GETFACL $ACLDIR 2> /dev/null | $EGREP -q "${acl_str2}"'
	log_pass "POSIX ACLs survive remount"
else
	log_fail "Group '${ZFS_ACL_STAFF_GROUP}' does not have 'rwx'"
fi

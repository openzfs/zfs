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
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

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

# Linux getfacl prints default: entries in the access listing.
# NetBSD getfacl prints default ACLs only with -d, without that prefix.
function posix_default_listed # dir
{
	typeset dir=$1
	getfacl "$dir" 2>/dev/null | grep -q "$acl_str2" && return 0
	getfacl -d "$dir" 2>/dev/null | grep -q "$acl_str1"
}

log_note "Testing access to DIRECTORY"
log_must mkdir $ACLDIR
if is_netbsd; then
	# GNU setfacl -d -m g:group:wx on mkdir 755 stores
	# default u::rwx,g::r-x,o::r-x,g:group:-wx,m::-wx.
	log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:-wx $ACLDIR
	log_must setfacl -d -m \
	    u::rwx,g::r-x,o::r-x,g:$ZFS_ACL_STAFF_GROUP:-wx,m::-wx \
	    $ACLDIR
else
	log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:wx $ACLDIR
	log_must setfacl -d -m g:$ZFS_ACL_STAFF_GROUP:wx $ACLDIR
fi

if getfacl $ACLDIR 2> /dev/null | grep -q "$acl_str1" &&
	posix_default_listed $ACLDIR
then
	# Drop any cwd hold on the mountpoint (NetBSD will not
	# unmount a busy dataset).  -f covers leftover namecache
	# / xattr vnodes from getfacl.
	cd / || true
	log_must zfs unmount -f $TESTPOOL/$TESTFS
	log_must zfs mount $TESTPOOL/$TESTFS
	log_must eval "getfacl $ACLDIR 2> /dev/null | grep -q \"$acl_str1\""
	if ! posix_default_listed $ACLDIR; then
		log_fail "default ACL for group '$ZFS_ACL_STAFF_GROUP' missing after remount"
	fi
	log_pass "POSIX ACLs survive remount"
else
	log_fail "Group '$ZFS_ACL_STAFF_GROUP' does not have 'rwx'"
fi

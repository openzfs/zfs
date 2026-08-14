#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# DESCRIPTION:
#	NFSv4 ACLs survive unmount/mount.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/dir.1
}

log_assert "Verify NFSv4 ACLs survive remount"
log_onexit cleanup

typeset ACLDIR="$TESTDIR/dir.1"
log_must mkdir $ACLDIR
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:rwx:allow $ACLDIR
if ! getfacl $ACLDIR 2>/dev/null | grep -q "group:$ZFS_ACL_STAFF_GROUP:"
then
	log_fail "NFSv4 ACL was not set"
fi

cd / || true
log_must zfs unmount -f $TESTPOOL/$TESTFS
log_must zfs mount $TESTPOOL/$TESTFS
if ! getfacl $ACLDIR 2>/dev/null | grep -q "group:$ZFS_ACL_STAFF_GROUP:"
then
	log_fail "NFSv4 ACL missing after remount"
fi

log_pass "NFSv4 ACLs survive remount"

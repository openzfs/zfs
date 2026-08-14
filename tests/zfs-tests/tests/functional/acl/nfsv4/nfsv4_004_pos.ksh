#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# DESCRIPTION:
#	acltype=nfsv4 rejects POSIX.1e ACL types (branding mismatch).
#	acltype=off is EOPNOTSUPP on VOP_GETACL/SETACL.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "both"

function cleanup
{
	rm -f $TESTDIR/file.brand
	zfs set acltype=nfsv4 $TESTPOOL/$TESTFS
}

log_assert "acltype=nfsv4 rejects POSIX types; acltype=off is EOPNOTSUPP"
log_onexit cleanup

log_must touch $TESTDIR/file.brand

# POSIX.1e default ACL is not an NFSv4 type.
log_mustnot setfacl -d -m u::rwx $TESTDIR/file.brand
# POSIX.1e access form against an NFSv4-branded file.
log_mustnot setfacl -m u::rwx,g::rx,o:: $TESTDIR/file.brand

log_must zfs set acltype=off $TESTPOOL/$TESTFS
# VOP_GETACL/SETACL return EOPNOTSUPP; setfacl cannot store a new ACL.
log_mustnot setfacl -m g:$ZFS_ACL_STAFF_GROUP:rwx:allow $TESTDIR/file.brand

log_pass "acltype branding: nfsv4 rejects POSIX, off is EOPNOTSUPP"

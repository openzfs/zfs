#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# DESCRIPTION:
#	acltype=nfsv4: directory ACL grants create, not listing.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/dir.0
}

log_assert "Verify acltype=nfsv4 works on a directory"
log_onexit cleanup

log_must mkdir $TESTDIR/dir.0
log_must chmod 700 $TESTDIR/dir.0
# write_data + execute + append, no read_data (cannot list)
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:wxp:allow $TESTDIR/dir.0

log_must user_run $ZFS_ACL_STAFF1 "touch $TESTDIR/dir.0/file.1"
log_mustnot user_run $ZFS_ACL_STAFF1 "ls $TESTDIR/dir.0"

log_pass "NFSv4 ACL mode works on directories"

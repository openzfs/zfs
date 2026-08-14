#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# DESCRIPTION:
#	acltype=nfsv4: setfacl/getfacl ACL_TYPE_NFS4, group can write.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "both"

function cleanup
{
	rm -f $TESTDIR/file.0
}

log_assert "Verify acltype=nfsv4 works on a file"
log_onexit cleanup

log_must touch $TESTDIR/file.0
log_must chmod 600 $TESTDIR/file.0
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:rwxp:allow $TESTDIR/file.0
if ! getfacl $TESTDIR/file.0 2>/dev/null |
    grep -q "group:$ZFS_ACL_STAFF_GROUP:"
then
	log_note "$(getfacl $TESTDIR/file.0 2>/dev/null)"
	log_fail "NFSv4 ACL for group '$ZFS_ACL_STAFF_GROUP' was not set"
fi

log_must user_run $ZFS_ACL_STAFF1 \
    "echo 'echo test > /dev/null' > $TESTDIR/file.0"

log_pass "NFSv4 ACL mode works on files"

#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# DESCRIPTION:
#	file_inherit / dir_inherit ACEs apply to created children
#	(aclinherit=passthrough from setup).
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/inh.d
}

log_assert "NFSv4 inherit flags apply to created children"
log_onexit cleanup

log_must mkdir $TESTDIR/inh.d
log_must chmod 0777 $TESTDIR/inh.d
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:rwx:fd:allow $TESTDIR/inh.d

log_must touch $TESTDIR/inh.d/child.f
log_must mkdir $TESTDIR/inh.d/child.d

if ! getfacl $TESTDIR/inh.d/child.f 2>/dev/null |
    grep -q "group:$ZFS_ACL_STAFF_GROUP:"
then
	log_note "$(getfacl $TESTDIR/inh.d/child.f 2>/dev/null)"
	log_fail "file_inherit ACE missing on child file"
fi
if ! getfacl $TESTDIR/inh.d/child.d 2>/dev/null |
    grep -q "group:$ZFS_ACL_STAFF_GROUP:"
then
	log_note "$(getfacl $TESTDIR/inh.d/child.d 2>/dev/null)"
	log_fail "dir_inherit ACE missing on child directory"
fi

log_pass "NFSv4 inherit flags apply to created children"

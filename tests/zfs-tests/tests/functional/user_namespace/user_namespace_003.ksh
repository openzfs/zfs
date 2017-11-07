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

. $STF_SUITE/tests/functional/user_namespace/user_namespace_common.kshlib

#
#
# DESCRIPTION:
#       Test mapping of users when using 'zfs allow'.
#
#
# STRATEGY:
#       1. Create datasets for users.
#       2. Delegate permissions to the unprivileged root user.
#       3. Verify 'zfs allow' that inside the namespace shows the correct user.
#       4. Verify that running 'zfs allow' inside a user namespace maps uids.
#

verify_runnable "both"

log_must add_group $STAFF_GROUP
log_must add_user $STAFF_GROUP $STAFF_USER
function cleanup
{
	log_must del_user $STAFF_USER
	log_must del_group $STAFF_GROUP
}
log_onexit cleanup

log_assert "Check user mapping in user namespaces"

# Test with a permission which needs no extra capabilities such as mounting...
typeset perm="snapshot_limit"
typeset perm_state_1="none"
typeset perm_state_2="3"

log_must zfs create -o ${perm}=${perm_state_1} $USER_TESTFS
log_must chown $ROOT_UID:$ROOT_UID $USER_TESTDIR

# allow the unprivileged root to pass on permissions
log_must zfs allow -u $ROOT_UID ${perm},allow $USER_TESTFS
# from within the user namespace, allow $STAFF_USER to create a dataset
log_must user_ns_exec zfs allow $STAFF_USER ${perm} $USER_TESTFS
# make sure the staff user *outside* the user namespace is not affected
log_mustnot chg_usr_exec $STAFF_USER zfs set ${perm}=${perm_state_2} $USER_TESTFS
# make sure the staff user *inside* the user namespace functions as expected
log_must user_ns_exec chg_usr_exec $STAFF_USER zfs set ${perm}=${perm_state_2} $USER_TESTFS

log_must user_ns_exec zfs unallow $STAFF_USER ${perm} $USER_TESTFS

log_must zfs destroy -r $USER_TESTFS

log_pass "Check user mapping in user namespaces"

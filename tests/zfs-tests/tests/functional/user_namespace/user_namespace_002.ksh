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
#       Test delegated permissions with user namespaces.
#
#
# STRATEGY:
#       1. Create datasets for users.
#       2. Delegate permissions to the unprivileged root user using numeric ids.
#       3. Try to create, mount and destroy datasets
#

verify_runnable "both"

log_assert "Check delegated permissions in user namespaces"

log_must zfs create $USER_TESTFS
log_must chown $ROOT_UID:$ROOT_UID $USER_TESTDIR

log_mustnot user_ns_exec zfs create $USER_TESTFS/subset

typeset perms="create,destroy,mount,mountpoint"

log_must zfs allow -u $ROOT_UID $perms $USER_TESTFS
log_must user_ns_exec zfs create -o mountpoint=legacy $USER_TESTFS/subset
log_must user_ns_exec zfs destroy $USER_TESTFS/subset
log_must zfs unallow -u $ROOT_UID $perms $USER_TESTFS
log_must zfs destroy $USER_TESTFS

log_pass "Check delegated permissions in user namespaces"

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
#       Regression test for secpolicy_vnode_setids_setgids
#
#
# STRATEGY:
#       1. Create files with various owners.
#       2. Try to set setgid bit.
#

verify_runnable "both"

# rroot: real root,
# uroot: root within user namespace
# uother: other user within user namespace
set -A files rroot_rroot uroot_uroot uroot_other uother_uroot uother_uother

function cleanup
{
	for i in ${files[*]}; do
		log_must rm -f $TESTDIR/$i
	done
}

unshare -Urm echo test
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi

log_onexit cleanup

log_assert "Check root in user namespaces"

TOUCH=$(readlink -f $(command -v touch))
CHMOD=$(readlink -f $(command -v chmod))

for i in ${files[*]}; do
	log_must $TOUCH $TESTDIR/$i
	log_must $CHMOD 0644 $TESTDIR/$i
done

log_must chown 0:0 $TESTDIR/rroot_rroot
log_must chown $ROOT_UID:$ROOT_UID $TESTDIR/uroot_uroot
log_must chown $ROOT_UID:$OTHER_UID $TESTDIR/uroot_other
log_must chown $OTHER_UID:$ROOT_UID $TESTDIR/uother_uroot
log_must chown $OTHER_UID:$OTHER_UID $TESTDIR/uother_uother

log_mustnot user_ns_exec $CHMOD 02755 $TESTDIR/rroot_rroot
log_mustnot test -g $TESTDIR/rroot_rroot

log_must user_ns_exec $CHMOD 02755 $TESTDIR/uroot_uroot
log_must test -g $TESTDIR/uroot_uroot

log_must user_ns_exec $CHMOD 02755 $TESTDIR/uroot_other
log_must test -g $TESTDIR/uroot_other

log_must user_ns_exec $CHMOD 02755 $TESTDIR/uother_uroot
log_must test -g $TESTDIR/uother_uroot

log_must user_ns_exec $CHMOD 02755 $TESTDIR/uother_uother
log_must test -g $TESTDIR/uother_uother

log_mustnot user_ns_exec $TOUCH $TESTDIR/rroot_rroot
log_must $CHMOD 0666 $TESTDIR/rroot_rroot
for i in ${files[*]}; do
	log_must user_ns_exec $TOUCH $TESTDIR/$i
done

log_pass "Check root in user namespaces"

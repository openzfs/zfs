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
# DESCRIPTION:
#	Regression test for delegation of datasets to user namespaces.
#
# STRATEGY:
#       1. Delegate a dataset to a user namespace.
#	2. Check that 'zfs list' is only able to see inside the delegation.
#	3. Check that 'zfs create' is able to create only inside the delegation.
#	4. Check that the filesystems can be mounted inside the delegation,
#	   and that file permissions are appropriate.
#       5. Check that 'zfs destroy' is able to destroy only inside the delegation.
#	6. Check that 'zfs unzone' has a desirable effect.
#

verify_runnable "both"

user_ns_cleanup() {
	if [ -n "$proc_ns_added" ]; then
		log_must zfs unzone $proc_ns_added $TESTPOOL/userns
	fi
	if [ -n "$unshared_pid" ]; then
		kill -9 $unshared_pid
		# Give it a sec to make the global cleanup more reliable.
		sleep 1
	fi
	log_must zfs destroy -r $TESTPOOL/userns
}

log_onexit user_ns_cleanup

log_assert "Check zfs/zpool command delegation in user namespaces"

# Create the baseline datasets.
log_must zfs create -o zoned=on $TESTPOOL/userns
log_must zfs create -o zoned=on $TESTPOOL/userns/testds
# Partial match should be denied; hence we also set this to be 'zoned'.
log_must zfs create -o zoned=on $TESTPOOL/user

# 1. Create a user namespace with a cloned mount namespace, then delegate.
unshare -Urm echo test
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi
unshare -Urm /usr/bin/sleep 1h &
unshared_pid=$!
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi
proc_ns=/proc/$unshared_pid/ns/user
sleep 2 # Wait for unshare to acquire user namespace
log_note "unshare: child=${unshared_pid} proc_ns=${proc_ns}"

NSENTER="nsenter -t $unshared_pid --all"

$NSENTER echo test
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to enter user namespace"
fi

# 1b. Pre-test by checking that 'zone' does something new.
list="$($NSENTER zfs list -r -H -o name | tr '\n' ' ')"
log_must test -z "$list"
log_must zfs zone $proc_ns $TESTPOOL/userns
proc_ns_added="$ns"

# 2. 'zfs list'
list="$($NSENTER zfs list -r -H -o name $TESTPOOL | tr '\n' ' ')"
log_must test "$list" = "$TESTPOOL $TESTPOOL/userns $TESTPOOL/userns/testds "

# 3. 'zfs create'
log_must $NSENTER zfs create $TESTPOOL/userns/created
log_mustnot $NSENTER zfs create $TESTPOOL/user/created

# 4. Check file permissions (create mounts the filesystem).  The 'permissions'
#    check is simply, does it get mapped to user namespace's root/root?
log_must $NSENTER df -h /$TESTPOOL/userns/created
log_must $NSENTER mkfile 8192 /$TESTPOOL/userns/created/testfile
uidgid=$($NSENTER stat -c '%u %g' /$TESTPOOL/userns/created/testfile)
log_must test "${uidgid}" = "0 0"

# 5. 'zfs destroy'
log_must $NSENTER zfs destroy $TESTPOOL/userns/created
log_mustnot $NSENTER zfs destroy $TESTPOOL/user

# 6. 'zfs unzone' should have an effect
log_must zfs unzone $proc_ns $TESTPOOL/userns
proc_ns_added=""
list="$($NSENTER zfs list -r -H -o name | tr '\n' ' ')"
log_must test -z "$list"

log_pass "Check zfs/zpool command delegation in user namespaces"

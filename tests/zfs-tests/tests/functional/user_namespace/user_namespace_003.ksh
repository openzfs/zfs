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

. $STF_SUITE/tests/functional/user_namespace/user_namespace_common.kshlib

#
# DESCRIPTION:
#	Regression test for delegation of datasets to user namespaces.
#
# STRATEGY:
#       1. Delegate two datasets with distinctive names to a user namespace.
#	2. Check that 'zfs list' is not able to see datasets outside of the
#	   delegation, which have a prefix matching one of the delegated sets.
#	   Also, check that all the delegated sets are visible.
#

verify_runnable "both"

user_ns_cleanup() {
	if [ -n "$proc_ns_added" ]; then
		log_must zfs unzone $proc_ns_added $TESTPOOL/userns
		log_must zfs unzone $proc_ns_added $TESTPOOL/otheruserns
	fi
	if [ -n "$unshared_pid" ]; then
		kill -9 $unshared_pid
		# Give it a sec to make the global cleanup more reliable.
		sleep 1
	fi
	log_must zfs destroy -r $TESTPOOL/userns
	log_must zfs destroy -r $TESTPOOL/usernsisitnot
	log_must zfs destroy -r $TESTPOOL/otheruserns
}

log_onexit user_ns_cleanup

log_assert "Check zfs list command handling of dataset visibility in user namespaces"

# Create the baseline dataset.
log_must zfs create -o zoned=on $TESTPOOL/userns
# Datasets with a prefix matching the delegated dataset should not be
# automatically considered visible.
log_must zfs create -o zoned=on $TESTPOOL/usernsisitnot
# All delegated datasets should be visible.
log_must zfs create -o zoned=on $TESTPOOL/otheruserns

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
log_must zfs zone $proc_ns $TESTPOOL/otheruserns
proc_ns_added="$proc_ns"

# 2. 'zfs list'
list="$($NSENTER zfs list -r -H -o name $TESTPOOL | tr '\n' ' ')"
log_must test "$list" = "$TESTPOOL $TESTPOOL/otheruserns $TESTPOOL/userns "

log_pass "Check zfs list command handling of dataset visibility in user namespaces"

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

. $STF_SUITE/tests/functional/pam/utilities.kshlib

if [ -n "$ASAN_OPTIONS" ]; then
	export LD_PRELOAD=$(ldd "$(command -v zfs)" | awk '/libasan\.so/ {print $3}')
fi

username="${username}rec"

# Set up a deeper hierarchy, a mountpoint that doesn't interfere with other tests,
# and a user which references that mountpoint
log_must zfs create "$TESTPOOL/pampam"
log_must zfs create -o mountpoint="$TESTDIR/rec" "$TESTPOOL/pampam/pam"
echo "recurpass" | zfs create -o encryption=aes-256-gcm -o keyformat=passphrase \
	-o keylocation=prompt "$TESTPOOL/pampam/pam/${username}"
log_must zfs unmount "$TESTPOOL/pampam/pam/${username}"
log_must zfs unload-key "$TESTPOOL/pampam/pam/${username}"
log_must add_user pamtestgroup ${username} "$TESTDIR/rec"

function keystatus {
	log_must [ "$(get_prop keystatus "$TESTPOOL/pampam/pam/${username}")" = "$1" ]
}

log_mustnot ismounted "$TESTPOOL/pampam/pam/${username}"
keystatus unavailable

function test_session {
	echo "recurpass" | pamtester ${pamservice} ${username} open_session
	references 1
	log_must ismounted "$TESTPOOL/pampam/pam/${username}"
	keystatus available

	log_must pamtester ${pamservice} ${username} close_session
	references 0
	log_mustnot ismounted "$TESTPOOL/pampam/pam/${username}"
	keystatus unavailable
}

genconfig "homes=$TESTPOOL/pampam/pam prop_mountpoint runstatedir=${runstatedir}"
test_session

genconfig "homes=$TESTPOOL/pampam recursive_homes prop_mountpoint runstatedir=${runstatedir}"
test_session

genconfig "homes=$TESTPOOL recursive_homes prop_mountpoint runstatedir=${runstatedir}"
test_session

genconfig "homes=* recursive_homes prop_mountpoint runstatedir=${runstatedir}"
test_session

log_pass "done."

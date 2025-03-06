#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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

. $STF_SUITE/tests/functional/pam/utilities.kshlib

if [ -n "$ASAN_OPTIONS" ]; then
	export LD_PRELOAD=$(ldd "$(command -v zfs)" | awk '/libasan\.so/ {print $3}')
fi

username="${username}mrec"

# Set up a deeper hierarchy, a mountpoint that doesn't interfere with other tests,
# and a user which references that mountpoint
log_must zfs create "$TESTPOOL/mrec"
log_must zfs create -o mountpoint="$TESTDIR/mrec" "$TESTPOOL/mrec/pam"
echo "recurpass" | zfs create -o encryption=aes-256-gcm -o keyformat=passphrase \
	-o keylocation=prompt "$TESTPOOL/mrec/pam/${username}"
log_must zfs create "$TESTPOOL/mrec/pam/${username}/deep"
log_must zfs create "$TESTPOOL/mrec/pam/${username}/deep/deeper"
log_must zfs create -o mountpoint=none "$TESTPOOL/mrec/pam/${username}/deep/none"
log_must zfs create -o canmount=noauto "$TESTPOOL/mrec/pam/${username}/deep/noauto"
log_must zfs create -o canmount=off "$TESTPOOL/mrec/pam/${username}/deep/off"
log_must zfs unmount "$TESTPOOL/mrec/pam/${username}"
log_must zfs unload-key "$TESTPOOL/mrec/pam/${username}"
log_must add_user pamtestgroup ${username} "$TESTDIR/mrec"

function keystatus {
	log_must [ "$(get_prop keystatus "$TESTPOOL/mrec/pam/${username}")" = "$1" ]
}

log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}"
keystatus unavailable

function test_session {
	echo "recurpass" | pamtester ${pamservice} ${username} open_session
	references 1
	log_must ismounted "$TESTPOOL/mrec/pam/${username}"
	log_must ismounted "$TESTPOOL/mrec/pam/${username}/deep"
	log_must ismounted "$TESTPOOL/mrec/pam/${username}/deep/deeper"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/none"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/noauto"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/off"
	keystatus available

	log_must pamtester ${pamservice} ${username} close_session
	references 0
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/deeper"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/none"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/noauto"
	log_mustnot ismounted "$TESTPOOL/mrec/pam/${username}/deep/off"
	keystatus unavailable
}

genconfig "homes=$TESTPOOL/mrec/pam mount_recursively runstatedir=${runstatedir}"
test_session

genconfig "homes=$TESTPOOL/mrec/pam prop_mountpoint mount_recursively runstatedir=${runstatedir}"
test_session

genconfig "homes=$TESTPOOL/mrec recursive_homes prop_mountpoint mount_recursively runstatedir=${runstatedir}"
test_session

genconfig "homes=$TESTPOOL recursive_homes prop_mountpoint mount_recursively runstatedir=${runstatedir}"
test_session

genconfig "homes=* recursive_homes prop_mountpoint mount_recursively runstatedir=${runstatedir}"
test_session

log_pass "done."

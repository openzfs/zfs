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

log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable
log_mustnot ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh unavailable

genconfig "homes=$TESTPOOL/pam,$TESTPOOL/pam-multi-home runstatedir=${runstatedir} nounmount"
echo "testpass" | pamtester ${pamservice} ${username} open_session
references 1
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus available
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh available

echo "testpass" | pamtester ${pamservice} ${username} open_session
references 2
keystatus available
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus_mh available
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"

log_must pamtester ${pamservice} ${username} close_session
references 1
keystatus available
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus_mh available
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"

log_must pamtester ${pamservice} ${username} close_session
references 0
keystatus available
keystatus_mh available
log_must ismounted "$TESTPOOL/pam/${username}"
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"
log_must zfs unmount "$TESTPOOL/pam/${username}"
log_must zfs unmount "$TESTPOOL/pam-multi-home/${username}"
log_must zfs unload-key "$TESTPOOL/pam/${username}"
log_must zfs unload-key "$TESTPOOL/pam-multi-home/${username}"

log_pass "done."

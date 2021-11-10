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

. $STF_SUITE/tests/functional/pam/utilities.kshlib

log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable

genconfig "homes=$TESTPOOL/pam runstatedir=${runstatedir} nounmount"
echo "testpass" | pamtester pam_zfs_key_test ${username} open_session
references 1
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus available

echo "testpass" | pamtester pam_zfs_key_test ${username} open_session
references 2
keystatus available
log_must ismounted "$TESTPOOL/pam/${username}"

log_must pamtester pam_zfs_key_test ${username} close_session
references 1
keystatus available
log_must ismounted "$TESTPOOL/pam/${username}"

log_must pamtester pam_zfs_key_test ${username} close_session
references 0
keystatus available
log_must ismounted "$TESTPOOL/pam/${username}"
log_must zfs unmount "$TESTPOOL/pam/${username}"
log_must zfs unload-key "$TESTPOOL/pam/${username}"

log_pass "done."

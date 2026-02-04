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

log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable
log_mustnot ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh unavailable

genconfig "homes=$TESTPOOL/pam,$TESTPOOL/pam-multi-home runstatedir=${runstatedir}"

printf "testpass\nsecondpass\nsecondpass\n" | pamtester -v ${pamservice} ${username} chauthtok

log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable
log_mustnot ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh unavailable

echo "secondpass" | pamtester ${pamservice} ${username} open_session
references 1
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus available
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh available

printf "secondpass\ntestpass\ntestpass\n" | pamtester -v ${pamservice} ${username} chauthtok

log_must ismounted "$TESTPOOL/pam/${username}"
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus available
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"
log_must ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh available

log_must pamtester ${pamservice} ${username} close_session
references 0
log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable
log_mustnot ismounted "$TESTPOOL/pam-multi-home/${username}"
keystatus_mh unavailable

log_pass "done."

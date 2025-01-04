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

command -v pamtester > /dev/null || log_unsupported "pam tests require the pamtester utility to be installed"
[ -f "$pammodule" ] || log_unsupported "$pammodule missing"

DISK=${DISKS%% *}
create_pool $TESTPOOL "$DISK"

log_must zfs create -o mountpoint="$TESTDIR" "$TESTPOOL/pam"
log_must add_group pamtestgroup
log_must add_user pamtestgroup ${username}
log_must mkdir -p "$runstatedir"

echo "testpass" | zfs create -o encryption=aes-256-gcm -o keyformat=passphrase -o keylocation=prompt "$TESTPOOL/pam/${username}"
log_must zfs unmount "$TESTPOOL/pam/${username}"
log_must zfs unload-key "$TESTPOOL/pam/${username}"

log_pass

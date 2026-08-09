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

command -v pamtester > /dev/null || log_unsupported "pam tests require the pamtester utility to be installed"
[ -f "$pammodule" ] || log_unsupported "$pammodule missing"

DISK=${DISKS%% *}
create_pool $TESTPOOL "$DISK"

log_must zfs create -o mountpoint="$TESTDIR" "$TESTPOOL/pam"
log_must zfs create -o mountpoint="$TESTDIR-multi-home" "$TESTPOOL/pam-multi-home"
log_must add_group pamtestgroup
log_must add_user pamtestgroup ${username}
log_must mkdir -p "$runstatedir"

echo "testpass" | zfs create -o encryption=aes-256-gcm -o keyformat=passphrase -o keylocation=prompt "$TESTPOOL/pam/${username}"
log_must zfs unmount "$TESTPOOL/pam/${username}"
log_must zfs unload-key "$TESTPOOL/pam/${username}"
echo "testpass" | zfs create -o encryption=aes-256-gcm -o keyformat=passphrase -o keylocation=prompt "$TESTPOOL/pam-multi-home/${username}"
log_must zfs unmount "$TESTPOOL/pam-multi-home/${username}"
log_must zfs unload-key "$TESTPOOL/pam-multi-home/${username}"

for i in {1..$PAM_MULTI_HOME_COUNT} ; do
       log_must zfs create -o mountpoint="$TESTDIR-multi-home$i" "$TESTPOOL/pam-multi-home$i"
       echo "testpass" | zfs create -o encryption=aes-256-gcm -o keyformat=passphrase -o keylocation=prompt "$TESTPOOL/pam-multi-home$i/${username}"
       log_must zfs unmount "$TESTPOOL/pam-multi-home$i/${username}"
       log_must zfs unload-key "$TESTPOOL/pam-multi-home$i/${username}"
done

log_pass

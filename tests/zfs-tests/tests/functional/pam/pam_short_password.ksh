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

#
# Copyright 2021 Attila Fülöp <attila@fueloep.org>
#


. $STF_SUITE/tests/functional/pam/utilities.kshlib

if [ -n "$ASAN_OPTIONS" ]; then
	export LD_PRELOAD=$(ldd "$(command -v zfs)" | awk '/libasan\.so/ {print $3}')
fi

if [[ -z pamservice ]]; then
	pamservice=pam_zfs_key_test
fi

# DESCRIPTION:
# If we set the encryption passphrase for a dataset via pam_zfs_key, a minimal
# passphrase length isn't enforced. This leads to a non-loadable key if
# `zfs load-key` enforces a minimal length. Make sure this isn't the case.

log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable

genconfig "homes=$TESTPOOL/pam runstatedir=${runstatedir}"

# Load keys and mount userdir.
echo "testpass" | pamtester ${pamservice} ${username} open_session
references 1
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus available

# Change user and dataset password to short one.
printf "testpass\nshort\nshort\n" | pamtester -v ${pamservice} ${username} chauthtok

# Unmount and unload key.
log_must pamtester ${pamservice} ${username} close_session
references 0
log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable

# Check if password change succeeded.
echo "testpass" | pamtester ${pamservice} ${username} open_session
references 1
log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable
log_must pamtester ${pamservice} ${username} close_session
references 0

echo "short" | pamtester ${pamservice} ${username} open_session
references 1
log_must ismounted "$TESTPOOL/pam/${username}"
keystatus available


# Finally check if `zfs load-key` succeeds with the short password.
log_must pamtester ${pamservice} ${username} close_session
references 0
log_mustnot ismounted "$TESTPOOL/pam/${username}"
keystatus unavailable

echo "short" | zfs load-key "$TESTPOOL/pam/${username}"
keystatus available
zfs unload-key "$TESTPOOL/pam/${username}"
keystatus unavailable

log_pass "done."

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
printf "short\nshort\n" | pamtester ${pamservice} ${username} chauthtok

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

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

#
# Copyright 2026 Colin K. Williams / LINK ORG LLC / LI-NK.SOCIAL. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

# Only run on Linux - zoned_uid is Linux-specific
if ! is_linux; then
	log_unsupported "zoned_uid is only supported on Linux"
fi

# Check kernel supports user namespaces
if ! [ -f /proc/self/uid_map ]; then
	log_unsupported "The kernel doesn't support user namespaces."
fi

verify_runnable "global"

DISK=${DISKS%% *}
default_setup_noexit $DISK

# Check if zoned_uid property is supported (requires pool to exist)
if ! zoned_uid_supported; then
	default_cleanup_noexit
	log_unsupported "zoned_uid property not supported by this kernel"
fi

#
# Provision test users if they don't exist.
# Tests use "sudo -u #<uid>" which requires the UID to have a passwd entry.
# CI environments (e.g. GitHub Actions QEMU VMs) typically don't have these.
#
for uid in "$ZONED_TEST_UID" "$ZONED_OTHER_UID"; do
	if ! id "$uid" >/dev/null 2>&1; then
		log_note "Creating test user for UID $uid"
		log_must useradd -u "$uid" -M -N -s /usr/sbin/nologin \
		    "zfs_test_$uid"
	fi
done

# Some environments (e.g., Ubuntu with AppArmor) restrict unprivileged
# user namespace creation.  Try to relax the restriction for testing.
APPARMOR_USERNS=/proc/sys/kernel/apparmor_restrict_unprivileged_userns
APPARMOR_RESTORE=/tmp/zoned_uid_apparmor_restore
if [ -f "$APPARMOR_USERNS" ]; then
	orig=$(cat "$APPARMOR_USERNS")
	if [ "$orig" != "0" ]; then
		echo "$orig" > "$APPARMOR_RESTORE"
		echo 0 > "$APPARMOR_USERNS"
		log_note "Relaxed AppArmor user namespace restriction for testing"
	fi
fi

# Verify user namespace creation works with the test UIDs.
if ! sudo -u \#${ZONED_TEST_UID} unshare --user --map-root-user \
    true 2>/dev/null; then
	default_cleanup_noexit
	log_unsupported "Cannot create user namespaces as UID $ZONED_TEST_UID"
fi

log_pass

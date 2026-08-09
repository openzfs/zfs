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
# Copyright 2026 Colin K. Williams / LINK ORG LLC / LI-NK.SOCIAL. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zoned_uid/zoned_uid.cfg

# Restore AppArmor user namespace restriction if we relaxed it
APPARMOR_USERNS=/proc/sys/kernel/apparmor_restrict_unprivileged_userns
APPARMOR_RESTORE=/tmp/zoned_uid_apparmor_restore
if [ -f "$APPARMOR_RESTORE" ]; then
	cat "$APPARMOR_RESTORE" > "$APPARMOR_USERNS"
	rm -f "$APPARMOR_RESTORE"
fi

# Remove test users created during setup
for uid in "$ZONED_TEST_UID" "$ZONED_OTHER_UID"; do
	if id "zfs_test_$uid" >/dev/null 2>&1; then
		userdel "zfs_test_$uid" 2>/dev/null
	fi
done

default_cleanup

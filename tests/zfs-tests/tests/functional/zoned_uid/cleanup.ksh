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

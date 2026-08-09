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

. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

#
# DESCRIPTION:
#	Verify that setting zoned_uid property does not break normal
#	dataset operations from the global zone.
#
# STRATEGY:
#	1. Create a test dataset with zoned_uid set
#	2. Verify dataset is still visible and accessible from global zone
#	3. Create a child dataset
#	4. Verify child dataset operations work
#	5. Verify the property is shown in zfs list output
#

verify_runnable "global"

function cleanup
{
	log_must zfs destroy -rf "$TESTPOOL/$TESTFS/zoned_test"
}

log_assert "zoned_uid property does not break global zone operations"
log_onexit cleanup

# Create test dataset with zoned_uid
log_must zfs create "$TESTPOOL/$TESTFS/zoned_test"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/zoned_test" "$ZONED_TEST_UID"

# Verify dataset is visible
log_must zfs list "$TESTPOOL/$TESTFS/zoned_test"
log_note "Dataset is visible from global zone"

# Verify we can get properties
log_must zfs get all "$TESTPOOL/$TESTFS/zoned_test"
log_note "Can retrieve properties from global zone"

# Verify zoned_uid appears in output
typeset list_output
list_output=$(zfs get -H -o property,value all "$TESTPOOL/$TESTFS/zoned_test" | grep zoned_uid)
if [[ -z "$list_output" ]]; then
	log_fail "zoned_uid not shown in property listing"
fi
log_note "zoned_uid appears in property listing: $list_output"

# Create child dataset
log_must zfs create "$TESTPOOL/$TESTFS/zoned_test/child"
log_note "Can create child dataset"

# Verify child is visible
log_must zfs list "$TESTPOOL/$TESTFS/zoned_test/child"
log_note "Child dataset is visible"

# Write data to the dataset
typeset mntpt
mntpt=$(get_prop mountpoint "$TESTPOOL/$TESTFS/zoned_test")
log_must touch "$mntpt/testfile"
log_must echo "test data" > "$mntpt/testfile"
log_note "Can write data to dataset"

# Read data back
log_must cat "$mntpt/testfile"
log_note "Can read data from dataset"

# Take a snapshot
log_must zfs snapshot "$TESTPOOL/$TESTFS/zoned_test@snap1"
log_note "Can create snapshot"

# List snapshots
log_must zfs list -t snapshot "$TESTPOOL/$TESTFS/zoned_test@snap1"
log_note "Snapshot is visible"

log_pass "zoned_uid property does not break global zone operations"

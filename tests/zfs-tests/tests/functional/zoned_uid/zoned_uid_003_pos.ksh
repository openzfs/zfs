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
	log_must zfs destroy -rf $TESTPOOL/$TESTFS/zoned_test
}

log_assert "zoned_uid property does not break global zone operations"
log_onexit cleanup

# Create test dataset with zoned_uid
log_must zfs create $TESTPOOL/$TESTFS/zoned_test
log_must set_zoned_uid $TESTPOOL/$TESTFS/zoned_test $ZONED_TEST_UID

# Verify dataset is visible
log_must zfs list $TESTPOOL/$TESTFS/zoned_test
log_note "Dataset is visible from global zone"

# Verify we can get properties
log_must zfs get all $TESTPOOL/$TESTFS/zoned_test
log_note "Can retrieve properties from global zone"

# Verify zoned_uid appears in output
typeset list_output=$(zfs get -H -o property,value all $TESTPOOL/$TESTFS/zoned_test | grep zoned_uid)
if [[ -z "$list_output" ]]; then
	log_fail "zoned_uid not shown in property listing"
fi
log_note "zoned_uid appears in property listing: $list_output"

# Create child dataset
log_must zfs create $TESTPOOL/$TESTFS/zoned_test/child
log_note "Can create child dataset"

# Verify child is visible
log_must zfs list $TESTPOOL/$TESTFS/zoned_test/child
log_note "Child dataset is visible"

# Write data to the dataset
typeset mntpt=$(get_prop mountpoint $TESTPOOL/$TESTFS/zoned_test)
log_must touch $mntpt/testfile
log_must echo "test data" > $mntpt/testfile
log_note "Can write data to dataset"

# Read data back
log_must cat $mntpt/testfile
log_note "Can read data from dataset"

# Take a snapshot
log_must zfs snapshot $TESTPOOL/$TESTFS/zoned_test@snap1
log_note "Can create snapshot"

# List snapshots
log_must zfs list -t snapshot $TESTPOOL/$TESTFS/zoned_test@snap1
log_note "Snapshot is visible"

log_pass "zoned_uid property does not break global zone operations"

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

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Removing the last special or dedup vdev returns the
#	allocation_classes feature to enabled.
#
# STRATEGY:
#	1. Create a non-raidz pool with a special and a dedup vdev.
#	2. Remove the dedup vdev: the feature stays active.
#	3. Remove a normal vdev: the feature stays active.
#	4. Remove the special vdev: the feature returns to enabled.
#	5. Export and import the pool: the feature is still enabled.
#	6. Add a special vdev: the feature is active again.
#

verify_runnable "global"

claim="Removing the last allocation class vdev deactivates the feature."

log_assert $claim
log_onexit cleanup

log_must disk_setup
log_must zpool create $TESTPOOL $ZPOOL_DISKS special $CLASS_DISK0 \
    dedup $CLASS_DISK1
check_feature $TESTPOOL active

log_must zpool remove -w $TESTPOOL $CLASS_DISK1
check_feature $TESTPOOL active

log_must zpool remove -w $TESTPOOL $ZPOOL_DISK2
check_feature $TESTPOOL active

log_must zpool remove -w $TESTPOOL $CLASS_DISK0
check_feature $TESTPOOL enabled

log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
check_feature $TESTPOOL enabled

log_must zpool add $TESTPOOL special $CLASS_DISK2
check_feature $TESTPOOL active

log_must zpool destroy -f $TESTPOOL

log_pass $claim

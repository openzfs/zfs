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
#	A vdev whose alloc_bias is special on disk keeps the
#	allocation_classes feature active when another vdev is removed,
#	even after a failed property update.
#
# STRATEGY:
#	1. Create a non-raidz pool of three normal vdevs.
#	2. Set the alloc_bias of the second one to special.
#	3. In one call, set its alloc_bias to none and its path to an
#	   invalid value: the call fails, so the bias stays special on disk.
#	4. Remove the third vdev: the feature stays active.
#	5. Export and import the pool: the feature is still active and the
#	   second vdev is special.
#

verify_runnable "global"

claim="A failed alloc_bias update does not deactivate the feature."

log_assert $claim
log_onexit cleanup

log_must disk_setup
log_must zpool create $TESTPOOL $ZPOOL_DISKS
check_feature $TESTPOOL enabled

log_must zpool set alloc_bias=special $TESTPOOL $ZPOOL_DISK1
check_feature $TESTPOOL active

# alloc_bias 0 is none; the path is refused because it is not under /dev.
guid=$(zpool get -H -o value guid $TESTPOOL $ZPOOL_DISK1)
log_mustnot vdev_set_props $TESTPOOL $guid alloc_bias=0 path=/invalid

log_must zpool remove -w $TESTPOOL $ZPOOL_DISK2
check_feature $TESTPOOL active

log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
check_feature $TESTPOOL active

bias=$(zpool get -H -o value alloc_bias $TESTPOOL $ZPOOL_DISK1)
[[ "$bias" == "special" ]] || log_fail "alloc_bias is '$bias', expected special"

log_must zpool destroy -f $TESTPOOL

log_pass $claim

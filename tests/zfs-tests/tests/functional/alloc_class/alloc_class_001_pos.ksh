#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017, Intel Corporation.
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Creating a pool with a special device succeeds, but only if
#	"feature@allocation_classes" is enabled.
#

verify_runnable "global"

claim="Creating a pool with a special device succeeds."

log_assert $claim
log_onexit cleanup

log_must disk_setup
for type in special dedup; do
	log_mustnot zpool create -d $TESTPOOL $CLASS_DISK0 $type $CLASS_DISK1
done
log_must zpool create $TESTPOOL raidz $ZPOOL_DISKS special mirror \
    $CLASS_DISK0 $CLASS_DISK1
log_must display_status "$TESTPOOL"
log_must zpool destroy -f "$TESTPOOL"

log_pass $claim
